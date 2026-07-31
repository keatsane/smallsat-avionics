/**
 * @file   rfm95.c
 * @brief  rfm95 lora driver - see rfm95.h for where it sits in the link plan
 *
 * register names and the bring-up order follow the sx1276 datasheet (rev 7), which is the chip
 * behind the rfm95 module. the module adds the antenna switch and the crystal and nothing else.
 */

#include "devices/rfm95.h"

#include "board.h"
#include "drivers/gpio.h"
#include "drivers/spi.h"
#include "drivers/systick.h"

// ---- sx1276 registers (lora page) ----
#define REG_FIFO           0x00U
#define REG_OP_MODE        0x01U
#define REG_FRF_MSB        0x06U
#define REG_FRF_MID        0x07U
#define REG_FRF_LSB        0x08U
#define REG_PA_CONFIG      0x09U
#define REG_FIFO_ADDR_PTR  0x0DU
#define REG_FIFO_TX_BASE   0x0EU
#define REG_FIFO_RX_BASE   0x0FU
#define REG_FIFO_RX_CURR   0x10U
#define REG_IRQ_FLAGS      0x12U
#define REG_RX_NB_BYTES    0x13U
#define REG_PAYLOAD_LENGTH 0x22U
#define REG_MODEM_CONFIG_1 0x1DU
#define REG_MODEM_CONFIG_2 0x1EU
#define REG_MODEM_CONFIG_3 0x26U
#define REG_PREAMBLE_MSB   0x20U
#define REG_PREAMBLE_LSB   0x21U
#define REG_SYNC_WORD      0x39U
#define REG_VERSION        0x42U

// op mode bits
#define MODE_LONG_RANGE 0x80U  // lora rather than fsk - only settable while asleep
#define MODE_SLEEP      0x00U
#define MODE_STDBY      0x01U
#define MODE_TX         0x03U
#define MODE_RX_CONT    0x05U

#define IRQ_TX_DONE   0x08U
#define IRQ_RX_DONE   0x40U
#define IRQ_CRC_ERROR 0x20U

#define SX1276_VERSION 0x12U  // what REG_VERSION reads on a working part

// 915 MHz, the ITU region 2 ISM band this rig lives in. the frequency register is in steps of
// F_XOSC / 2^19 = 32e6 / 524288 = 61.035 Hz, so frf = freq / 61.035
#define LORA_FREQ_HZ 915000000UL
#define FRF_STEP_HZ  61.03515625

// a private sync word, so this link ignores every other lora device in range. 0x34 is reserved
// for LoRaWAN and is deliberately avoided - this is a point-to-point link, not a network join
#define PRIVATE_SYNC_WORD 0x12U

static bool configured = false;

// whether a transmit is actually in the air. the TX_DONE irq flag alone cannot answer this: it
// reads zero on a radio that has never transmitted, which is indistinguishable from "still
// sending" - and gating the first send on it deadlocks the beacon permanently. found on the
// bench, where the ground station heard nothing at all
static bool tx_busy = false;
static uint32_t tx_sent = 0U;  // frames handed to the radio since boot

// ---- register access - bit 7 of the address selects write ----

static uint8_t reg_read(uint8_t reg) {
    spi_select(spi_lora);
    spi_transfer_byte(spi_lora, reg & 0x7FU);
    const uint8_t v = spi_transfer_byte(spi_lora, 0x00U);
    spi_deselect(spi_lora);
    return v;
}

static void reg_write(uint8_t reg, uint8_t val) {
    spi_select(spi_lora);
    spi_transfer_byte(spi_lora, reg | 0x80U);
    spi_transfer_byte(spi_lora, val);
    spi_deselect(spi_lora);
}

// the mode bits and the lora bit live in one register, and the lora bit only takes while the
// chip is asleep - so every mode change goes through sleep on the way in
static void set_mode(uint8_t mode) { reg_write(REG_OP_MODE, MODE_LONG_RANGE | mode); }

bool rfm95_init(void) {
    configured = false;

    spi_lora_init();

    // reset is active low and the datasheet wants 100 us low then 5 ms to settle. done here
    // rather than trusting power-on state: a warm reset of the mcu does not reset the radio, so
    // without this the driver would be configuring a chip in whatever mode it was left in
    gpio_enable_port(LORA_RST_PORT);
    gpio_config_output(LORA_RST_PORT, LORA_RST_PIN);
    LORA_RST_PORT->BSRR = (1U << (LORA_RST_PIN + 16U));  // low
    delay_ms(1U);
    LORA_RST_PORT->BSRR = (1U << LORA_RST_PIN);  // high
    delay_ms(5U);

    // dio0 carries tx-done. an input for now and polled - the interrupt is worth having only
    // once something needs to sleep waiting for it
    gpio_enable_port(LORA_DIO0_PORT);
    gpio_config_input(LORA_DIO0_PORT, LORA_DIO0_PIN);

    const bool locked = spi_bus_lock(spi_lora);

    // prove the part is there before writing a single configuration byte
    if (reg_read(REG_VERSION) != SX1276_VERSION) {
        spi_bus_unlock(spi_lora, locked);
        return false;
    }

    set_mode(MODE_SLEEP);  // lora bit only latches from sleep
    delay_ms(1U);

    const uint32_t frf = (uint32_t)(LORA_FREQ_HZ / FRF_STEP_HZ);
    reg_write(REG_FRF_MSB, (uint8_t)(frf >> 16));
    reg_write(REG_FRF_MID, (uint8_t)(frf >> 8));
    reg_write(REG_FRF_LSB, (uint8_t)frf);

    // the fifo is split now that the link runs both ways: transmit from 0, receive from 0x80.
    // a shared base would have an inbound command overwrite an outbound beacon mid-air
    reg_write(REG_FIFO_TX_BASE, 0x00U);
    reg_write(REG_FIFO_RX_BASE, 0x80U);
    reg_write(REG_FIFO_ADDR_PTR, 0x00U);

    // 125 kHz bandwidth, 4/5 coding, explicit header; sf7, crc on. sf7 is the fastest spreading
    // factor and the shortest range, which is the right trade on a bench where the ground station
    // is a metre away - it keeps air time short so the beacon does not eat the duty cycle
    reg_write(REG_MODEM_CONFIG_1, 0x72U);
    reg_write(REG_MODEM_CONFIG_2, 0x74U);
    reg_write(REG_MODEM_CONFIG_3, 0x04U);  // agc on

    reg_write(REG_PREAMBLE_MSB, 0x00U);
    reg_write(REG_PREAMBLE_LSB, 0x08U);
    reg_write(REG_SYNC_WORD, PRIVATE_SYNC_WORD);

    // PA_BOOST at 17 dBm. the rfm95 wires its antenna to PA_BOOST and not to RFO, so selecting
    // RFO transmits into a disconnected pin and the link simply never works
    reg_write(REG_PA_CONFIG, 0x8FU);

    set_mode(MODE_STDBY);
    tx_busy = false;
    tx_sent = 0U;
    configured = (reg_read(REG_OP_MODE) == (MODE_LONG_RANGE | MODE_STDBY));

    // and then listen. the radio spends almost all of its time here - a beacon is 57 ms once a
    // second, so better than 90% of every second is available to hear the ground
    if (configured) {
        set_mode(MODE_RX_CONT);
    }
    spi_bus_unlock(spi_lora, locked);
    return configured;
}

bool rfm95_alive(void) {
    if (!configured) {
        return false;
    }
    const bool locked = spi_bus_lock(spi_lora);
    const bool ok = (reg_read(REG_VERSION) == SX1276_VERSION);
    spi_bus_unlock(spi_lora, locked);
    return ok;
}

bool rfm95_tx_done(void) {
    if (!configured) {
        return true;  // nothing in flight on a radio that never came up
    }
    if (!tx_busy) {
        return true;  // never started one, or the last one already completed
    }

    const bool locked = spi_bus_lock(spi_lora);
    if ((reg_read(REG_IRQ_FLAGS) & IRQ_TX_DONE) != 0U) {
        reg_write(REG_IRQ_FLAGS, IRQ_TX_DONE);  // write-1-to-clear
        tx_busy = false;
        set_mode(MODE_RX_CONT);  // straight back to listening - transmitting is the exception
    }
    spi_bus_unlock(spi_lora, locked);
    return !tx_busy;
}

bool rfm95_send(const uint8_t* data, size_t len) {
    if (!configured || data == NULL || len == 0U || len > 255U) {
        return false;
    }

    const bool locked = spi_bus_lock(spi_lora);

    set_mode(MODE_STDBY);
    reg_write(REG_IRQ_FLAGS, 0xFFU);  // write-1-to-clear, so this clears the last tx-done
    reg_write(REG_FIFO_ADDR_PTR, 0x00U);

    // the fifo auto-increments, so the whole payload goes out under one chip select rather than
    // one transaction per byte
    spi_select(spi_lora);
    spi_transfer_byte(spi_lora, REG_FIFO | 0x80U);
    for (size_t i = 0U; i < len; i++) {
        spi_transfer_byte(spi_lora, data[i]);
    }
    spi_deselect(spi_lora);

    reg_write(REG_PAYLOAD_LENGTH, (uint8_t)len);
    set_mode(MODE_TX);
    tx_busy = true;
    tx_sent++;

    spi_bus_unlock(spi_lora, locked);
    return true;
}

size_t rfm95_receive(uint8_t* buf, size_t max) {
    if (!configured || buf == NULL || max == 0U || tx_busy) {
        return 0U;  // mid-transmit the radio is not listening, and cannot be asked to
    }

    const bool locked = spi_bus_lock(spi_lora);
    size_t n = 0U;

    const uint8_t flags = reg_read(REG_IRQ_FLAGS);
    if ((flags & IRQ_RX_DONE) != 0U) {
        reg_write(REG_IRQ_FLAGS, IRQ_RX_DONE | IRQ_CRC_ERROR);

        // the radio's own crc already rejected a corrupt packet, so a bad one is dropped here
        // rather than handed up - the frame crc behind it would catch it anyway, twice is fine
        if ((flags & IRQ_CRC_ERROR) == 0U) {
            const uint8_t len = reg_read(REG_RX_NB_BYTES);
            reg_write(REG_FIFO_ADDR_PTR, reg_read(REG_FIFO_RX_CURR));
            n = (len > max) ? max : len;
            for (size_t i = 0U; i < n; i++) {
                buf[i] = reg_read(REG_FIFO);
            }
        }
    }

    spi_bus_unlock(spi_lora, locked);
    return n;
}

uint32_t rfm95_sent(void) { return tx_sent; }

uint8_t rfm95_reg(uint8_t reg) {
    const bool locked = spi_bus_lock(spi_lora);
    const uint8_t v = reg_read(reg);
    spi_bus_unlock(spi_lora, locked);
    return v;
}
