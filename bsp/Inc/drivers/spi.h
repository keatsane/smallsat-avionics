/**
 * @file   spi.h
 * @brief  spi driver - polled master, one handle per spi instance
 */

#ifndef SPI_H
#define SPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// opaque per-instance handle
typedef struct spi spi_t;

// spi2 -> pb12-15: icm-20948 imu
extern spi_t* const spi_imu;

/**
 * @brief  bring up the imu spi (spi2, pb12(CS)/pb13(SCK)/pb14(MISO)/pb15(MOSI), af5 for 13/14/15)
 * at the given max_hz, mode 0, msb first
 * @param  max_hz  ceiling for the sck rate - init picks the fastest pclk1/2^n prescaler at or under
 * it
 */
void spi_imu_init(uint32_t max_hz);

/**
 * @brief    transfer a byte over spi
 * @param	 s	  spi peripheral to transfer with
 * @param    tx   byte to send
 * @return   byte received
 */
uint8_t spi_transfer(spi_t* s, uint8_t tx);

/**
 * @brief    pull cs down for desired device before starting a transfer
 * @param	 s	  spi peripheral
 */
void spi_select(spi_t* s);

/**
 * @brief    raise cs after the last byte has fully left the wire (waits on busy)
 * @param	 s	  spi peripheral
 */
void spi_deselect(spi_t* s);

// bus error counts since boot
typedef struct {
    uint32_t overrun;  // hardware lost a byte before it was read
    uint32_t modf;     // another master tried to grab the bus (nss pulled low as master)
} spi_errors_t;

/**
 * @brief  copy the current bus error counts
 * @param  s    target spi handle
 * @param  out  destination for the counts
 */
void spi_get_errors(spi_t* s, spi_errors_t* out);

#ifdef __cplusplus
}
#endif

#endif  // SPI_H
