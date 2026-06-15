/**
 * @file   icm20948.h
 * @brief  icm-20948 driver - 9-dof accel/gyro/mag over spi
 */

#ifndef ICM20948_H
#define ICM20948_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t t_ms;          // acquisition time
    int16_t accel[3];       // raw counts: x, y, z
    int16_t gyro[3];        // raw counts: x, y, z
    int16_t mag[3];         // raw counts: x, y, z
    bool accel_gyro_valid;  // accel + gyro read ok (false = not responding, e.g. unplugged)
    bool mag_valid;         // mag read ok (ak09916 reported fresh data with no overflow)
} icm20948_sample_t;

/**
 * @brief  wake and configure the imu
 * @return true if who_am_i read, false otherwise
 */
bool icm20948_init(void);

/**
 * @brief  read imu sample (accel, gyro, mag), stamp time, validity
 * @return imu sample filled with accel/gyro/mag counts, timestamp, validity
 */
icm20948_sample_t icm20948_read(void);

#ifdef __cplusplus
}
#endif

#endif  // ICM20948_H
