/**
 * @file   spi.c
 * @brief  spi driver - polled master, one handle per spi instance
 */

#include "drivers/spi.h"

#include "drivers/clock.h"
#include "stm32f446xx.h"

// per-instance state
struct spi {
    SPI_TypeDef* regs;
    GPIO_TypeDef* cs_port;  // chip select gpio port

    uint32_t cs_pin;  // chip select gpio pin

    spi_errors_t errors;  // bus error counts
};

static struct spi imu_inst = {.regs = SPI2, .cs_port = GPIOB, .cs_pin = 12U};

spi_t* const spi_imu = &imu_inst;

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

void spi_imu_init(uint32_t max_hz) {
    // spi2 on apb1, gpios: pb12(CS)/pb13(SCK)/pb14(MISO)/pb15(MOSI)
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
    (void)RCC->APB1ENR;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR;

    GPIOB->BSRR = GPIO_BSRR_BS12;  // cs idles high - latch it before pb12 becomes an output

    GPIOB->MODER &= ~(GPIO_MODER_MODE12_Msk | GPIO_MODER_MODE13_Msk | GPIO_MODER_MODE14_Msk |
                      GPIO_MODER_MODE15_Msk);
    GPIOB->MODER |= (GPIO_MODER_MODE12_0 | GPIO_MODER_MODE13_1 | GPIO_MODER_MODE14_1 |
                     GPIO_MODER_MODE15_1);  // 12 output, 13/14/15 alternate function
    GPIOB->OSPEEDR |=
        (GPIO_OSPEEDR_OSPEED13_0 | GPIO_OSPEEDR_OSPEED15_0);  // medium speed for sck/mosi
    GPIOB->AFR[1] &= ~(GPIO_AFRH_AFSEL13_Msk | GPIO_AFRH_AFSEL14_Msk | GPIO_AFRH_AFSEL15_Msk);
    GPIOB->AFR[1] |= (0x5UL << GPIO_AFRH_AFSEL13_Pos) | (0x5UL << GPIO_AFRH_AFSEL14_Pos) |
                     (0x5UL << GPIO_AFRH_AFSEL15_Pos);

    spi_start(spi_imu, clock_pclk1_hz(), max_hz);
}

uint8_t spi_transfer(spi_t* s, uint8_t tx) {
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

void spi_select(spi_t* s) { s->cs_port->BSRR = (1U << (s->cs_pin + 16U)); }

void spi_deselect(spi_t* s) {
    while (s->regs->SR & SPI_SR_BSY) {
    }  // txe lies - last bits may still be shifting
    s->cs_port->BSRR = (1U << s->cs_pin);
}

void spi_get_errors(spi_t* s, spi_errors_t* out) { *out = s->errors; }
