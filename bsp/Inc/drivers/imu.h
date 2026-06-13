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
    int16_t accel[3];  // high/low bytes combined for raw: x, y, z
    int16_t gyro[3];   // high/low bytes combined for raw: x, y, z
    int16_t mag[3];    // high/low bytes combined for raw: x, y, z
    uint32_t t_ms;     // acquisition time
    bool valid;        // false until proven; never use invalid sample
} imu_sample_t;

/**
 * @brief  wake and configure the imu
 * @return true if who_am_i read, false otherwise
 */
bool imu_init(void);

/**
 * @brief read imu sample (accel, gyro, mag), stamp time, validity
 * @param out  destination sample - filled with accel/gyro/mag counts, timestamp, validity
 */
void imu_read(imu_sample_t* out);

#ifdef __cplusplus
}
#endif

#endif  // IMU_H
