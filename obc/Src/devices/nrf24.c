/**
 * @file   nrf24.c
 * @brief  nrf24l01+ driver - see nrf24.h for where it sits in the link plan
 *
 * register and command names follow the nrf24l01+ datasheet (rev 1.0).
 */

#include "devices/nrf24.h"

#include "board.h"
#include "drivers/gpio.h"
#include "drivers/spi.h"
#include "drivers/systick.h"

// ---- spi commands ----
#define CMD_R_REGISTER   0x00U  // or'd with the register address
#define CMD_W_REGISTER   0x20U
#define CMD_W_TX_PAYLOAD 0xA0U
#define CMD_FLUSH_TX     0xE1U
#define CMD_NOP          0xFFU

// ---- registers ----
#define REG_CONFIG      0x00U
#define REG_EN_AA       0x01U
#define REG_EN_RXADDR   0x02U
#define REG_SETUP_AW    0x03U
#define REG_SETUP_RETR  0x04U
#define REG_RF_CH       0x05U
#define REG_RF_SETUP    0x06U
#define REG_STATUS      0x07U
#define REG_TX_ADDR     0x10U
#define REG_FIFO_STATUS 0x17U

// CONFIG: mask every irq (this driver polls), 2-byte crc, powered up, transmit mode
#define CONFIG_TX 0x7EU

#define STATUS_TX_FULL    0x01U
#define FIFO_TX_EMPTY     0x10U
#define ADDR_WIDTH        5U
#define NRF24_MAX_PAYLOAD 32U

// channel 76 - above the 2.4 GHz band's busiest wifi overlap, and the part's own default in most
// reference designs. the ground station has to agree, so this is a link parameter not a taste
#define RF_CHANNEL 76U

// 1 Mbps, minimum transmit power.
//
// not the part's 2 Mbps top speed on purpose: 2 Mbps costs ~3 dB of receiver sensitivity and these
// PA+LNA modules are known to be fussy at it, and 1 Mbps is already about 180x the LoRa beacon.
//
// the power bits are 00 (-18 dBm at the chip) rather than 11 (0 dBm), which is the setting a link
// budget would call absurd and a bench needs. both ends carry a power amplifier and a low-noise
// amplifier, and they sit a foot apart: at full output the receiver's LNA is driven far past the
// linear range, and an overloaded front end does not degrade gracefully - it just stops decoding.
// measured at full power, 570 packets went out and 224 arrived. the module's own PA still adds
// about 20 dB on top of this, so there is plenty of margin for a room
#define RF_SETUP_1MBPS 0x00U

// the link's address, shared with the ground station. "SSAV1" in ascii - any five bytes work as
// long as both ends agree, and something legible beats a magic number nobody can check
static const uint8_t tx_address[ADDR_WIDTH] = {0x53U, 0x53U, 0x41U, 0x56U, 0x31U};

static bool configured = false;
static uint32_t dropped = 0U;
static uint32_t sent = 0U;  // packets accepted by the fifo since boot

// ---- register access ----

static uint8_t reg_read(uint8_t reg) {
    spi_select(spi_nrf24);
    spi_transfer_byte(spi_nrf24, CMD_R_REGISTER | reg);
    const uint8_t v = spi_transfer_byte(spi_nrf24, CMD_NOP);
    spi_deselect(spi_nrf24);
    return v;
}

static void reg_write(uint8_t reg, uint8_t val) {
    spi_select(spi_nrf24);
    spi_transfer_byte(spi_nrf24, CMD_W_REGISTER | reg);
    spi_transfer_byte(spi_nrf24, val);
    spi_deselect(spi_nrf24);
}

static void reg_write_buf(uint8_t reg, const uint8_t* buf, size_t n) {
    spi_select(spi_nrf24);
    spi_transfer_byte(spi_nrf24, CMD_W_REGISTER | reg);
    for (size_t i = 0U; i < n; i++) {
        spi_transfer_byte(spi_nrf24, buf[i]);
    }
    spi_deselect(spi_nrf24);
}

static void reg_read_buf(uint8_t reg, uint8_t* buf, size_t n) {
    spi_select(spi_nrf24);
    spi_transfer_byte(spi_nrf24, CMD_R_REGISTER | reg);
    for (size_t i = 0U; i < n; i++) {
        buf[i] = spi_transfer_byte(spi_nrf24, CMD_NOP);
    }
    spi_deselect(spi_nrf24);
}

static void cmd(uint8_t c) {
    spi_select(spi_nrf24);
    spi_transfer_byte(spi_nrf24, c);
    spi_deselect(spi_nrf24);
}

// write one packet, up to 32 bytes. returns false when the fifo has no room
static bool write_packet(const uint8_t* data, size_t n) {
    if ((reg_read(REG_STATUS) & STATUS_TX_FULL) != 0U) {
        dropped++;
        return false;
    }

    // always a full 32 bytes, padded. the receiver runs static payload width, which means it has
    // to know the size in advance - and the alternative, dynamic payload length, requires
    // auto-ack, which is off here so the vehicle never stalls waiting on a ground station that
    // may not be listening.
    //
    // the padding is free of consequence because the frame decoder resyncs on AA 55 and checks a
    // crc: trailing zeros between frames are skipped exactly like line noise on a uart. it costs
    // 96 bytes of air per 70-byte frame, which at 1 Mbps is nothing worth optimising
    spi_select(spi_nrf24);
    spi_transfer_byte(spi_nrf24, CMD_W_TX_PAYLOAD);
    for (size_t i = 0U; i < NRF24_MAX_PAYLOAD; i++) {
        spi_transfer_byte(spi_nrf24, (i < n) ? data[i] : 0x00U);
    }
    spi_deselect(spi_nrf24);
    sent++;
    return true;
}

bool nrf24_init(void) {
    configured = false;
    dropped = 0U;
    sent = 0U;

    spi_nrf24_init();

    // ce gates the radio and is held high for the whole session. the usual pattern pulses it for
    // 10 us per packet, which would need a microsecond delay this project does not have - held
    // high the part sits in standby-II and transmits whatever lands in the fifo, which is the
    // same result with no timing to get wrong
    gpio_enable_port(NRF24_CE_PORT);
    NRF24_CE_PORT->BSRR = (1U << (NRF24_CE_PIN + 16U));  // low while configuring
    gpio_config_output(NRF24_CE_PORT, NRF24_CE_PIN);

    gpio_enable_port(NRF24_IRQ_PORT);
    gpio_config_input(NRF24_IRQ_PORT, NRF24_IRQ_PIN);

    delay_ms(100U);  // power-on-reset settle - the datasheet wants 100 ms from supply valid

    const bool locked = spi_bus_lock(spi_nrf24);

    reg_write(REG_CONFIG, CONFIG_TX);
    delay_ms(2U);  // power-down to standby is 1.5 ms

    // no auto-ack and no retransmits: this is a one-way downlink and there is nothing on the far
    // end yet. with auto-ack on, every packet would retry to its limit and stall the link against
    // a receiver that does not exist. acks become worth having when the ground station does
    reg_write(REG_EN_AA, 0x00U);
    reg_write(REG_EN_RXADDR, 0x00U);
    reg_write(REG_SETUP_RETR, 0x00U);

    reg_write(REG_SETUP_AW, 0x03U);  // 5-byte addresses
    reg_write(REG_RF_CH, RF_CHANNEL);
    reg_write(REG_RF_SETUP, RF_SETUP_1MBPS);
    reg_write_buf(REG_TX_ADDR, tx_address, ADDR_WIDTH);

    cmd(CMD_FLUSH_TX);
    reg_write(REG_STATUS, 0x70U);  // write-1-to-clear the three irq flags

    // no version register on this part, so prove it is there by reading back what was just
    // written. five matching bytes is not something a floating bus produces
    uint8_t readback[ADDR_WIDTH] = {0};
    reg_read_buf(REG_TX_ADDR, readback, ADDR_WIDTH);

    configured = true;
    for (size_t i = 0U; i < ADDR_WIDTH; i++) {
        if (readback[i] != tx_address[i]) {
            configured = false;
        }
    }

    spi_bus_unlock(spi_nrf24, locked);

    if (configured) {
        NRF24_CE_PORT->BSRR = (1U << NRF24_CE_PIN);  // and now the radio is live
    }
    return configured;
}

bool nrf24_alive(void) {
    if (!configured) {
        return false;
    }
    const bool locked = spi_bus_lock(spi_nrf24);
    const bool ok = (reg_read(REG_CONFIG) == CONFIG_TX);
    spi_bus_unlock(spi_nrf24, locked);
    return ok;
}

bool nrf24_tx_empty(void) {
    if (!configured) {
        return true;
    }
    const bool locked = spi_bus_lock(spi_nrf24);
    const bool empty = (reg_read(REG_FIFO_STATUS) & FIFO_TX_EMPTY) != 0U;
    spi_bus_unlock(spi_nrf24, locked);
    return empty;
}

bool nrf24_send(const uint8_t* data, size_t len) {
    if (!configured || data == NULL || len == 0U) {
        return false;
    }

    const bool locked = spi_bus_lock(spi_nrf24);

    // split across packets. the far end concatenates them back into a byte stream and the frame
    // decoder finds its own boundaries in it, exactly as it does on a uart - so a lost packet
    // costs one frame to a crc failure rather than desynchronising the link
    // `off` rather than `sent`, which is the name of the file-scope packet counter - the shadow
    // compiled and worked, and is exactly the kind of thing that reads correct while meaning
    // something else
    bool ok = true;
    size_t off = 0U;
    while (off < len) {
        size_t n = len - off;
        if (n > NRF24_MAX_PAYLOAD) {
            n = NRF24_MAX_PAYLOAD;
        }
        if (!write_packet(&data[off], n)) {
            ok = false;
            break;  // fifo full - the caller is outrunning the air, and waiting here would block
        }
        off += n;
    }

    spi_bus_unlock(spi_nrf24, locked);
    return ok;
}

uint32_t nrf24_dropped(void) { return dropped; }

uint32_t nrf24_sent(void) { return sent; }
