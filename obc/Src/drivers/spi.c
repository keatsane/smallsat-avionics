/**
 * @file   spi.c
 * @brief  spi driver - polled master, one handle per spi instance
 */

#include "drivers/spi.h"

#include "board.h"
#include "drivers/clock.h"
#include "drivers/gpio.h"
#include "stm32f446xx.h"

#define SPI_IMU_MAX_HZ    7000000U  // 7 mhz ceiling for the ICM-20948 (next step down is 4 mhz)
#define SPI_CAMERA_MAX_HZ 8000000U  // arducam fifo; pclk1/8 (5.625 mhz) lands just under this

// per-instance state
struct spi {
    SPI_TypeDef* regs;
    GPIO_TypeDef* cs_port;  // chip select gpio port

    uint32_t cs_pin;  // chip select gpio pin

    spi_errors_t errors;  // bus error counts
};

// spi_init fills regs/cs into the handle from the cfg below, so the instances start empty
static struct spi spi_imu_inst;
spi_t* const spi_imu = &spi_imu_inst;

static struct spi spi_camera_inst;
spi_t* const spi_camera = &spi_camera_inst;

// all that differs between buses: the peripheral, its clock-enable bit, the pin map, the ceiling
typedef struct {
    SPI_TypeDef* regs;
    uint32_t apb1_enr_bit;  // clock-enable bit; spi2 and spi3 both live on apb1
    GPIO_TypeDef* io_port;  // sck/miso/mosi share one port
    uint32_t sck_pin;
    uint32_t miso_pin;
    uint32_t mosi_pin;
    uint32_t af;
    GPIO_TypeDef* cs_port;  // cs can sit on a different port (the camera's does)
    uint32_t cs_pin;
    uint32_t max_hz;
} spi_cfg_t;

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

// bring up one bus from its cfg: clock on, cs gpio (idle high), the three af pins, then enable
static void spi_init(spi_t* s, const spi_cfg_t* c) {
    s->regs = c->regs;
    s->cs_port = c->cs_port;
    s->cs_pin = c->cs_pin;

    RCC->APB1ENR |= c->apb1_enr_bit;
    (void)RCC->APB1ENR;
    gpio_enable_port(c->io_port);
    gpio_enable_port(c->cs_port);  // no-op if cs shares io_port (imu), needed when it doesn't (cam)

    // cs idles high - latch it before the pin becomes an output
    c->cs_port->BSRR = (1U << c->cs_pin);
    gpio_config_output(c->cs_port, c->cs_pin);

    gpio_config_af(c->io_port, c->sck_pin, c->af, GPIO_PUSH_PULL, GPIO_SPEED_MEDIUM);
    gpio_config_af(c->io_port, c->miso_pin, c->af, GPIO_PUSH_PULL, GPIO_SPEED_LOW);
    gpio_config_af(c->io_port, c->mosi_pin, c->af, GPIO_PUSH_PULL, GPIO_SPEED_MEDIUM);

    spi_start(s, clock_pclk1_hz(), c->max_hz);
}

void spi_imu_init(void) {
    static const spi_cfg_t cfg = {
        .regs = IMU_SPI,
        .apb1_enr_bit = RCC_APB1ENR_SPI2EN,
        .io_port = IMU_SPI_PORT,
        .sck_pin = IMU_SCK_PIN,
        .miso_pin = IMU_MISO_PIN,
        .mosi_pin = IMU_MOSI_PIN,
        .af = IMU_SPI_AF,
        .cs_port = IMU_CS_PORT,
        .cs_pin = IMU_CS_PIN,
        .max_hz = SPI_IMU_MAX_HZ,
    };
    spi_init(spi_imu, &cfg);
}

void spi_camera_init(void) {
    static const spi_cfg_t cfg = {
        .regs = CAMERA_SPI,
        .apb1_enr_bit = RCC_APB1ENR_SPI3EN,
        .io_port = CAMERA_SPI_PORT,
        .sck_pin = CAMERA_SCK_PIN,
        .miso_pin = CAMERA_MISO_PIN,
        .mosi_pin = CAMERA_MOSI_PIN,
        .af = CAMERA_SPI_AF,
        .cs_port = CAMERA_CS_PORT,
        .cs_pin = CAMERA_CS_PIN,
        .max_hz = SPI_CAMERA_MAX_HZ,
    };
    spi_init(spi_camera, &cfg);
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
