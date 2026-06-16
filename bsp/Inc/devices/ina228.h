/**
 * @file   ina228.h
 * @brief  ina228 driver - 20-bit precise power/energy/charge monitor over I2C
 */

#ifndef INA228_H
#define INA228_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t t_ms;       // acquisition time
    uint32_t bus_mv;     // bus voltage (millivolts)
    int32_t current_ma;  // current (milliamps)
    uint32_t power_mw;   // power (milliwatts)
    int16_t dietemp_cc;  // die temperature (centi degree celcius)
    bool valid;
} ina228_sample_t;

/**
 * @brief  verify the ids and configure the power monitor (reset, shunt cal, adc config)
 * @return true if the manufacturer and device id match, false otherwise
 */
bool ina228_init(void);

/**
 * @brief  read a power sample (bus voltage, current, power, die temp), stamp time and validity
 * @return sample with bus_mv/current_ma/power_mw/dietemp_cc, a timestamp, and a validity flag
 */
ina228_sample_t ina228_read(void);

#ifdef __cplusplus
}
#endif

#endif  // INA228_H
