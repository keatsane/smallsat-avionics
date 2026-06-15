/**
 * @file   i2c.c
 * @brief  i2c driver - polled master, one handle per i2c instance
 */

#include "drivers/i2c.h"

#include "drivers/clock.h"
#include "drivers/gpio.h"
#include "drivers/systick.h"
#include "stm32f446xx.h"

#define I2C_SCL_HZ     400000U  // fast-mode scl frequency (the slowest device on the bus caps this)
#define I2C_TIMEOUT_MS 5U       // a 400 khz byte is ~25 us; 5 ms with no progress means a hung bus

// per-instance state
struct i2c {
    I2C_TypeDef* regs;
};

static struct i2c i2c_sensors_inst = {.regs = I2C1};

i2c_t* const i2c_sensors = &i2c_sensors_inst;

// the part that's identical for every i2c: timing off pclk1
static void i2c_start(i2c_t* i) {
    uint32_t pclk = clock_pclk1_hz();
    uint32_t pclk_mhz = pclk / 1000000U;

    i->regs->CR1 |= I2C_CR1_SWRST;  // reset to a clean state
    i->regs->CR1 &= ~I2C_CR1_SWRST;

    // fast mode formulas: FREQ = pclk in MHz, CCR = pclk / (3 * scl), TRISE = pclk * 300 ns + 1
    i->regs->CR2 = pclk_mhz;
    i->regs->CCR = I2C_CCR_FS | (pclk / (3U * I2C_SCL_HZ));
    i->regs->TRISE = (pclk_mhz * 300U) / 1000U + 1U;

    i->regs->OAR1 = (1UL << 14);  // bit 14 must be kept at 1 per the rm

    i->regs->CR1 |= I2C_CR1_PE;  // enable
}

void i2c_sensors_init(void) {
    // enable the i2c peripheral and its gpio port; pins pb8(SCL)/pb9(SDA)
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    (void)RCC->APB1ENR;
    gpio_enable_port(GPIOB);

    gpio_config_af(GPIOB, 8U, 4U, GPIO_OPEN_DRAIN, GPIO_SPEED_MEDIUM);  // scl
    gpio_config_af(GPIOB, 9U, 4U, GPIO_OPEN_DRAIN, GPIO_SPEED_MEDIUM);  // sda

    i2c_start(i2c_sensors);
}

// the bus can be held hostage (clock stretch, stuck line)
static i2c_status_t wait_bus_free(i2c_t* i) {
    uint32_t t0 = millis();
    while (i->regs->SR2 & I2C_SR2_BUSY) {
        if ((millis() - t0) >= I2C_TIMEOUT_MS) {
            return I2C_ERR_TIMEOUT;
        }
    }
    return I2C_OK;
}

// spin until SR1 shows `flag`
static i2c_status_t wait_flag(i2c_t* i, uint32_t flag) {
    uint32_t t0 = millis();
    while (!(i->regs->SR1 & flag)) {
        if ((millis() - t0) >= I2C_TIMEOUT_MS) {
            i->regs->CR1 |= I2C_CR1_STOP;
            return I2C_ERR_TIMEOUT;
        }
    }
    return I2C_OK;
}

// wait for the slave to ack its address
static i2c_status_t wait_addr(i2c_t* i) {
    uint32_t t0 = millis();
    while (!(i->regs->SR1 & I2C_SR1_ADDR)) {
        if (i->regs->SR1 & I2C_SR1_AF) {  // ack failure (no answer)
            i->regs->SR1 &= ~I2C_SR1_AF;
            i->regs->CR1 |= I2C_CR1_STOP;
            return I2C_ERR_NACK;
        }
        if ((millis() - t0) >= I2C_TIMEOUT_MS) {
            i->regs->CR1 |= I2C_CR1_STOP;
            return I2C_ERR_TIMEOUT;
        }
    }
    return I2C_OK;
}

i2c_status_t i2c_read_regs(i2c_t* i, uint8_t addr, uint8_t reg, uint8_t* buf, size_t n) {
    i2c_status_t st;

    if ((st = wait_bus_free(i)) != I2C_OK) {
        return st;
    }

    // address the device for a write and send the register pointer
    i->regs->CR1 |= I2C_CR1_ACK;
    i->regs->CR1 |= I2C_CR1_START;
    if ((st = wait_flag(i, I2C_SR1_SB)) != I2C_OK) {
        return st;
    }
    i->regs->DR = (uint8_t)(addr << 1U);  // write mode, reading sb then writing dr clears sb
    if ((st = wait_addr(i)) != I2C_OK) {
        return st;
    }
    (void)i->regs->SR1;  // clear addr: read sr1 then sr2
    (void)i->regs->SR2;
    if ((st = wait_flag(i, I2C_SR1_TXE)) != I2C_OK) {
        return st;
    }
    i->regs->DR = reg;
    if ((st = wait_flag(i, I2C_SR1_BTF)) != I2C_OK) {
        return st;
    }

    // repeated start, re-address the same device for a read
    i->regs->CR1 |= I2C_CR1_START;
    if ((st = wait_flag(i, I2C_SR1_SB)) != I2C_OK) {
        return st;
    }
    i->regs->DR = (uint8_t)((addr << 1U) | 1U);  // read mode
    if ((st = wait_addr(i)) != I2C_OK) {
        return st;
    }

    // pull n bytes, the f4 buffers two deep (DR + shift), so the ack/stop for the tail
    // must be armed before those bytes land
    if (n == 1U) {
        i->regs->CR1 &= ~I2C_CR1_ACK;  // nack the only byte
        __disable_irq();     // clear-addr then stop must be atomic or a 2nd byte sneaks in
        (void)i->regs->SR2;  // clear addr (sr1 already read in wait_addr)
        i->regs->CR1 |= I2C_CR1_STOP;
        __enable_irq();
        if ((st = wait_flag(i, I2C_SR1_RXNE)) != I2C_OK) {
            return st;
        }
        buf[0] = (uint8_t)i->regs->DR;
    } else if (n == 2U) {
        i->regs->CR1 |= I2C_CR1_POS;  // ack/nack now applies to the next byte, not the current
        __disable_irq();
        (void)i->regs->SR2;            // clear addr
        i->regs->CR1 &= ~I2C_CR1_ACK;  // nack the 2nd byte
        __enable_irq();
        if ((st = wait_flag(i, I2C_SR1_BTF)) != I2C_OK) {  // both bytes buffered, clock stretched
            return st;
        }
        __disable_irq();
        i->regs->CR1 |= I2C_CR1_STOP;
        buf[0] = (uint8_t)i->regs->DR;
        __enable_irq();
        buf[1] = (uint8_t)i->regs->DR;
        i->regs->CR1 &= ~I2C_CR1_POS;  // leave pos as we found it
    } else {
        (void)i->regs->SR2;  // clear addr
        size_t b = 0;
        while (n - b > 3U) {  // bulk read, ack'ing each, until the last three remain
            if ((st = wait_flag(i, I2C_SR1_BTF)) != I2C_OK) {
                return st;
            }
            buf[b++] = (uint8_t)i->regs->DR;
        }
        if ((st = wait_flag(i, I2C_SR1_BTF)) != I2C_OK) {  // two bytes buffered, clock stretched
            return st;
        }
        i->regs->CR1 &= ~I2C_CR1_ACK;  // the next byte to land is the last - nack it
        __disable_irq();
        buf[b++] = (uint8_t)i->regs->DR;  // read, freeing the shift reg to take the final byte
        i->regs->CR1 |= I2C_CR1_STOP;     // stop after that final byte
        __enable_irq();
        buf[b++] = (uint8_t)i->regs->DR;
        if ((st = wait_flag(i, I2C_SR1_RXNE)) != I2C_OK) {
            return st;
        }
        buf[b++] = (uint8_t)i->regs->DR;
    }

    return I2C_OK;
}

i2c_status_t i2c_write_regs(i2c_t* i, uint8_t addr, uint8_t reg, const uint8_t* buf, size_t n) {
    i2c_status_t st;

    if ((st = wait_bus_free(i)) != I2C_OK) {
        return st;
    }

    // start + address the device for a write
    i->regs->CR1 |= I2C_CR1_START;
    if ((st = wait_flag(i, I2C_SR1_SB)) != I2C_OK) {
        return st;
    }
    i->regs->DR = (uint8_t)(addr << 1U);  // write mode
    if ((st = wait_addr(i)) != I2C_OK) {
        return st;
    }
    (void)i->regs->SR1;  // clear addr
    (void)i->regs->SR2;

    // register pointer, then the payload (the device auto-increments across the write)
    if ((st = wait_flag(i, I2C_SR1_TXE)) != I2C_OK) {
        return st;
    }
    i->regs->DR = reg;
    for (size_t b = 0; b < n; b++) {
        if ((st = wait_flag(i, I2C_SR1_TXE)) != I2C_OK) {
            return st;
        }
        i->regs->DR = buf[b];
    }

    // wait for the last byte to fully shift out (BTF), then stop
    if ((st = wait_flag(i, I2C_SR1_BTF)) != I2C_OK) {
        return st;
    }
    i->regs->CR1 |= I2C_CR1_STOP;
    return I2C_OK;
}
