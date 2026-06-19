/**
 * @file   tmp117.h
 * @brief  tmp117 driver - I2C high accuracy digital temperature sensor
 */

#ifndef TMP117_H
#define TMP117_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t t_ms;    // acquisition time
    int32_t temp_mc;  // temperature (milli-degrees C), signed
    bool valid;
} tmp117_sample_t;

/**
 * @brief  verify the device id over i2c; the tmp117 boots into continuous conversion at its
 *         defaults, so there is nothing else to configure
 * @return true if the device id matches, false otherwise
 */
bool tmp117_init(void);

/**
 * @brief  read a temperature sample, stamp time and validity
 * @return sample with temp_mc (milli-degrees C), a timestamp, and a validity flag
 */
tmp117_sample_t tmp117_read(void);

#ifdef __cplusplus
}
#endif

#endif  // TMP117_H
