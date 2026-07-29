/**
 * @file   tmp117.c
 * @brief  tmp117 driver - I2C high accuracy digital temperature sensor
 */

#include "devices/tmp117.h"

#include "drivers/i2c.h"
#include "drivers/systick.h"

#define TMP117_ADDR 0x48U  // addr0 -> gnd

// registers
#define TEMP_RESULT 0x00  // signed 16-bit, 7.8125 mdegC/LSB

// confirm communication
#define DEVICE_ID 0x0F  // low 12 bits read 0x117

bool tmp117_init(void) {
    // nothing to configure - the tmp117 boots into continuous conversion at its defaults,
    // so init is just the id gate
    uint8_t buf[2];
    if (i2c_read_regs(i2c_sensors, TMP117_ADDR, DEVICE_ID, buf, sizeof(buf)) != I2C_OK) {
        return false;
    }

    // top 4 bits are a revision field, the device id is the low 12
    return (((uint16_t)(buf[0] << 8) | buf[1]) & 0x0FFFU) == 0x0117U;
}

tmp117_sample_t tmp117_read(void) {
    tmp117_sample_t sample;
    uint8_t buf[2] = {0, 0};
    bool ok = i2c_read_regs(i2c_sensors, TMP117_ADDR, TEMP_RESULT, buf, sizeof(buf)) == I2C_OK;

    // signed 16-bit at 7.8125 mdegC/LSB (= 125/16) -> milli-degrees C
    int16_t raw = (int16_t)((buf[0] << 8) | buf[1]);
    sample.temp_mc = ((int32_t)raw * 125) / 16;

    sample.t_ms = millis();
    sample.valid = ok;
    return sample;
}
