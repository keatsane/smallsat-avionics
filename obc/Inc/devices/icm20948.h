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

// counts per degree/second at the +-2000 dps full scale the driver configures (GYRO_CONFIG_1 =
// 0x35). the datasheet's sensitivity table gives 32.8 LSB/dps there. changing the full scale
// means changing this - they are one decision, so they live next to each other
#define ICM20948_GYRO_LSB_PER_DPS 16.4F

// a boot bias further from zero than this says the rig was moving while the 64-sample
// calibration ran, so the offset absorbed real motion and every later reading is over-corrected.
// ~6 deg/s, comfortably outside the datasheet's typical zero-rate offset and comfortably inside
// anything a hand-knock produces. it matters most in POINTING, which integrates rate into a
// heading and turns a small constant error into drift that grows all session
#define ICM20948_GYRO_BIAS_SUSPECT 200

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

/**
 * @brief  read back the gyro zero-rate offset measured at boot
 * @param  out  filled with the x/y/z bias in raw counts
 */
void icm20948_gyro_bias(int16_t out[3]);

#ifdef __cplusplus
}
#endif

#endif  // ICM20948_H
