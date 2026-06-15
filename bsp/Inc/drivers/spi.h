/**
 * @file   spi.h
 * @brief  spi driver - polled master, one handle per spi instance
 */

#ifndef SPI_H
#define SPI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// opaque per-instance handle
typedef struct spi spi_t;

// spi2 -> pb12-15: icm-20948 imu
extern spi_t* const spi_imu;

// bus error counts since boot
typedef struct {
    uint32_t overrun;  // hardware lost a byte before it was read
    uint32_t modf;     // another master tried to grab the bus (nss pulled low as master)
} spi_errors_t;

/**
 * @brief  bring up the imu spi (spi2, pb12(CS)/pb13(SCK)/pb14(MISO)/pb15(MOSI), af5 for 13/14/15),
 * mode 0, msb first, clocked at the fastest pclk1/2^n prescaler at or under the imu's 7 mhz ceiling
 */
void spi_imu_init(void);

/**
 * @brief  transfer a single byte over spi
 * @param  s   spi peripheral to transfer with
 * @param  tx  byte to send
 * @return byte received
 */
uint8_t spi_transfer_byte(spi_t* s, uint8_t tx);

/**
 * @brief  transfer a block over spi (full-duplex; pass NULL to clock out 0xff / discard rx)
 * @param  s   spi peripheral to transfer with
 * @param  tx  bytes to send, or NULL to send 0xff
 * @param  rx  destination for received bytes, or NULL to discard
 * @param  n   number of bytes
 */
void spi_transfer_buf(spi_t* s, const uint8_t* tx, uint8_t* rx, size_t n);

/**
 * @brief  pull cs down for the target device before a transfer
 * @param  s  spi peripheral
 */
void spi_select(spi_t* s);

/**
 * @brief  raise cs after the last byte has fully left the wire (waits on busy)
 * @param  s  spi peripheral
 */
void spi_deselect(spi_t* s);

/**
 * @brief  snapshot the current bus error counts
 * @param  s  target spi handle
 * @return the error counts
 */
spi_errors_t spi_get_errors(spi_t* s);

#ifdef __cplusplus
}
#endif

#endif  // SPI_H
