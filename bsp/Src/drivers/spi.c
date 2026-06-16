/**
 * @file   spi.c
 * @brief  spi driver - polled master, one handle per spi instance
 */

#include "drivers/spi.h"

#include "board.h"
#include "drivers/clock.h"
#include "drivers/gpio.h"
#include "stm32f446xx.h"

#define SPI_IMU_MAX_HZ 7000000U  // 7 mhz ceiling for the ICM-20948 (next step down is 4 mhz)

// per-instance state
struct spi {
    SPI_TypeDef* regs;
    GPIO_TypeDef* cs_port;  // chip select gpio port

    uint32_t cs_pin;  // chip select gpio pin

    spi_errors_t errors;  // bus error counts
};

static struct spi spi_imu_inst = {.regs = IMU_SPI, .cs_port = IMU_CS_PORT, .cs_pin = IMU_CS_PIN};

spi_t* const spi_imu = &spi_imu_inst;

// the part that's identical for every spi: max_hz, set master, enable spi
static void spi_start(spi_t* s, uint32_t pclk_hz, uint32_t max_hz) {
    // smallest br (fastest sck) with pclk / 2^(br+1) <= max_hz; br=7 = /256 floor
    uint32_t br = 0U;
    while (br < 7U && (pclk_hz >> (br + 1U)) > max_hz) {
        br++;
    }

    s->regs->CR1 = (br << SPI_CR1_BR_Pos);
    s->regs->CR1 |= SPI_CR1_MSTR;
    s->regs->CR1 |= SPI_CR1_SSM;
    s->regs->CR1 |= SPI_CR1_SSI;
    s->regs->CR1 |= SPI_CR1_SPE;
}

void spi_imu_init(void) {
    // spi2 on apb1; pin map in board.h
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
    (void)RCC->APB1ENR;
    gpio_enable_port(IMU_SPI_PORT);

    // cs idles high - latch it before the pin becomes an output
    IMU_CS_PORT->BSRR = (1U << IMU_CS_PIN);
    IMU_CS_PORT->MODER &= ~(0x3UL << (IMU_CS_PIN * 2U));
    IMU_CS_PORT->MODER |= (0x1UL << (IMU_CS_PIN * 2U));  // output (cs)

    gpio_config_af(IMU_SPI_PORT, IMU_SCK_PIN, IMU_SPI_AF, GPIO_PUSH_PULL, GPIO_SPEED_MEDIUM);
    gpio_config_af(IMU_SPI_PORT, IMU_MISO_PIN, IMU_SPI_AF, GPIO_PUSH_PULL, GPIO_SPEED_LOW);
    gpio_config_af(IMU_SPI_PORT, IMU_MOSI_PIN, IMU_SPI_AF, GPIO_PUSH_PULL, GPIO_SPEED_MEDIUM);

    spi_start(spi_imu, clock_pclk1_hz(), SPI_IMU_MAX_HZ);
}

uint8_t spi_transfer_byte(spi_t* s, uint8_t tx) {
    while (!(s->regs->SR & SPI_SR_TXE)) {
    }  // room in the waiting room?
    s->regs->DR = tx;  // drop the byte in - this starts the clock
    while (!(s->regs->SR & SPI_SR_RXNE)) {
    }  // 8 ticks later the swapped byte lands
    uint8_t rx = (uint8_t)s->regs->DR;  // reading dr also clears rxne

    uint32_t sr = s->regs->SR;
    if (sr & SPI_SR_OVR) {
        s->errors.overrun++;
        (void)s->regs->DR;  // ovr clear sequence: read dr
        (void)s->regs->SR;  // then sr
    }
    if (sr & SPI_SR_MODF) {
        s->errors.modf++;
        s->regs->CR1 |= SPI_CR1_MSTR;  // modf clear: sr already read, rewrite cr1
    }

    return rx;
}

void spi_transfer_buf(spi_t* s, const uint8_t* tx, uint8_t* rx, size_t n) {
    for (size_t i = 0U; i < n; i++) {
        uint8_t in = spi_transfer_byte(s, tx != NULL ? tx[i] : 0xFFU);
        if (rx != NULL) {
            rx[i] = in;
        }
    }
}

void spi_select(spi_t* s) { s->cs_port->BSRR = (1U << (s->cs_pin + 16U)); }

void spi_deselect(spi_t* s) {
    while (s->regs->SR & SPI_SR_BSY) {
    }  // txe lies - last bits may still be shifting
    s->cs_port->BSRR = (1U << s->cs_pin);
}

spi_errors_t spi_get_errors(spi_t* s) { return s->errors; }
