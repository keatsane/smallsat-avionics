/**
 * @file   spi.h
 * @brief  spi driver - polled master, one handle per spi instance
 */

#ifndef SPI_H
#define SPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// opaque per-instance handle
typedef struct spi spi_t;

// spi2 -> pb12-15: icm-20948 imu
extern spi_t* const spi_imu;

// spi3 -> pc10-12 + pb0(cs): arducam ov2640 fifo
extern spi_t* const spi_camera;

// the same spi3 wires with the lora radio's chip select (pb7). a second handle rather than a
// second bus: one peripheral, one bus lock, three chip selects
extern spi_t* const spi_lora;

// and the nrf24's chip select, same spi3 wires again
extern spi_t* const spi_nrf24;

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
 * @brief  bring up the camera spi (spi3, pb0(CS)/pc10(SCK)/pc11(MISO)/pc12(MOSI), af6 for
 * 10/11/12), mode 0, msb first, clocked at the fastest pclk1/2^n prescaler at or under the arducam
 * 8 mhz ceiling
 */
void spi_camera_init(void);

/**
 * @brief  bring up the lora chip select on the already-running spi3
 * Only the cs pin - the bus itself is configured by spi_camera_init, and configuring one
 * peripheral twice from two places is how two drivers end up disagreeing about its clock. Call
 * after spi_camera_init.
 */
void spi_lora_init(void);

/**
 * @brief  bring up the nrf24 chip select on the already-running spi3
 * Only the cs pin, same reasoning as spi_lora_init. Call after spi_camera_init.
 */
void spi_nrf24_init(void);

/**
 * @brief  create the per-bus mutexes
 * Separate from the init functions on purpose, same as uart_locks_init: board bring-up must not
 * call into the kernel. Call once after the board is up and before the scheduler starts.
 */
void spi_locks_init(void);

/**
 * @brief  claim the bus this device sits on, blocking until it is free
 * @param  s  any handle on the bus
 * @return true if the lock was taken and must be released
 *
 * The lock belongs to the peripheral, not the handle: SPI3 carries the camera and both radios,
 * so what two tasks contend for is the wires. A device driver holds this across a whole
 * transaction - and for the camera that includes closing its FIFO burst, because a burst leaves
 * chip select asserted and a radio clocking the bus underneath it would corrupt both.
 */
bool spi_bus_lock(spi_t* s);

/**
 * @brief  release the bus
 * @param  s       the same handle passed to spi_bus_lock
 * @param  locked  what spi_bus_lock returned
 */
void spi_bus_unlock(spi_t* s, bool locked);

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
