/**
 * @file   imu.h
 * @brief  imu driver - accessing accelerometer, gyroscope, and magnetometer on ICM-20948
 */

#ifndef IMU_H
#define IMU_H

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
} imu_sample_t;

/**
 * @brief  wake and configure the imu
 * @return true if who_am_i read, false otherwise
 */
bool imu_init(void);

/**
 * @brief  read imu sample (accel, gyro, mag), stamp time, validity
 * @return imu sample filled with accel/gyro/mag counts, timestamp, validity
 */
imu_sample_t imu_read(void);

#ifdef __cplusplus
}
#endif

#endif  // IMU_H
