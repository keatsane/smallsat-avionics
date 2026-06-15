/**
 * @file   i2c.h
 * @brief  i2c driver - polled master, one handle per i2c instance
 */

#ifndef I2C_H
#define I2C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// opaque per-instance handle
typedef struct i2c i2c_t;

// i2c1 -> pb8-9: ina228 power monitor, tmp117 temperature sensor, ...
extern i2c_t* const i2c_sensors;

// per-call result of one I2C transaction
typedef enum {
    I2C_OK = 0,
    I2C_ERR_NACK,     // slave didn't ACK - absent / wrong addr / not responding
    I2C_ERR_BUS,      // BERR - misplaced start/stop or glitch
    I2C_ERR_TIMEOUT,  // our software timeout - the bus hung
} i2c_status_t;

/**
 * @brief  bring up i2c1 on pb8(SCL)/pb9(SDA) - af4, open-drain, fast mode (400 khz)
 */
void i2c_sensors_init(void);

/**
 * @brief  read n bytes from a device register over i2c
 * @param  i     target i2c handle
 * @param  addr  7-bit device address
 * @param  reg   starting register - the device auto-increments across the read
 * @param  buf   destination for the n bytes
 * @param  n     number of bytes to read
 * @return I2C_OK, or an I2C_ERR_* code on nack / bus error / timeout
 */
i2c_status_t i2c_read_regs(i2c_t* i, uint8_t addr, uint8_t reg, uint8_t* buf, size_t n);

/**
 * @brief  write n bytes to a device register over i2c
 * @param  i     target i2c handle
 * @param  addr  7-bit device address
 * @param  reg   destination register - the device auto-increments across the write
 * @param  buf   the n bytes to write (msb first for a multi-byte register)
 * @param  n     number of bytes to write
 * @return I2C_OK, or an I2C_ERR_* code on nack / bus error / timeout
 */
i2c_status_t i2c_write_regs(i2c_t* i, uint8_t addr, uint8_t reg, const uint8_t* buf, size_t n);

#ifdef __cplusplus
}
#endif

#endif  // I2C_H
