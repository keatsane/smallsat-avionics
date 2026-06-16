/**
 * @file   ina228.c
 * @brief  ina228 driver - 20-bit precise power/energy/charge monitor over I2C
 */

#include "devices/ina228.h"

#include "drivers/i2c.h"
#include "drivers/systick.h"

#define INA228_ADDR 0x40U  // a0,a1 -> gnd

// registers
// cofiguration / calibration
#define CONFIG     0x00
#define ADC_CONFIG 0x01
#define SHUNT_CAL  0x02

// measurements / results
#define VBUS    0x05
#define DIETEMP 0x06
#define CURRENT 0x07
#define POWER   0x08

// confirm communication
#define MANUFACTURER_ID 0x3E
#define DEVICE_ID       0x3F

// shunt calibration: 10 uA/count gives a ~5.2 A ceiling (2^19 * 10 uA), well above the avionics bus
#define VAL_CURRENT_LSB 10U    // resolution: 10 uA per current count
#define VAL_SHUNT_CAL   1966U  // computed from CURRENT_LSB (10 uA) + the 15 mohm shunt

bool ina228_init(void) {
    uint8_t buf[2];
    if (i2c_read_regs(i2c_sensors, INA228_ADDR, MANUFACTURER_ID, buf, sizeof(buf)) != I2C_OK) {
        return false;
    }

    if (((uint16_t)(buf[0] << 8) | buf[1]) != 0x5449U) {  // MANFID = 5449 (TI)
        return false;
    }

    if (i2c_read_regs(i2c_sensors, INA228_ADDR, DEVICE_ID, buf, sizeof(buf)) != I2C_OK) {
        return false;
    }

    if ((((uint16_t)(buf[0] << 8) | buf[1]) >> 4) != 0x228U) {  // DIEID = 228
        return false;
    }

    // reset to defaults - CONFIG bit 15 (RST) is the high byte's msb, self-clears
    buf[0] = 0x80U;
    buf[1] = 0x00U;
    if (i2c_write_regs(i2c_sensors, INA228_ADDR, CONFIG, buf, sizeof(buf)) != I2C_OK) {
        return false;
    }
    delay_ms(2U);  // let the reset settle before configuring

    // shunt calibration - ties shunt voltage to amps; without it CURRENT/POWER read zero
    buf[0] = (uint8_t)(VAL_SHUNT_CAL >> 8);
    buf[1] = (uint8_t)(VAL_SHUNT_CAL & 0xFFU);
    if (i2c_write_regs(i2c_sensors, INA228_ADDR, SHUNT_CAL, buf, sizeof(buf)) != I2C_OK) {
        return false;
    }

    // adc config - continuous bus+shunt+temp at ~1ms, reset default (0xFB68)
    buf[0] = 0xFBU;
    buf[1] = 0x68U;
    if (i2c_write_regs(i2c_sensors, INA228_ADDR, ADC_CONFIG, buf, sizeof(buf)) != I2C_OK) {
        return false;
    }

    return true;
}

// read n big-endian bytes from a register into one value
static uint32_t read_be(uint8_t reg, size_t n, bool* ok) {
    uint8_t buf[3];
    if (i2c_read_regs(i2c_sensors, INA228_ADDR, reg, buf, n) != I2C_OK) {
        *ok = false;
    }

    uint32_t v = 0U;
    for (size_t i = 0U; i < n; i++) {
        v = (v << 8) | buf[i];
    }

    return v;
}

ina228_sample_t ina228_read(void) {
    ina228_sample_t sample;
    bool ok = true;

    uint32_t vbus = read_be(VBUS, 3U, &ok);             // 24-bit, data in [23:4]
    uint32_t curr = read_be(CURRENT, 3U, &ok);          // 24-bit signed, data in [23:4]
    uint32_t pwr = read_be(POWER, 3U, &ok);             // 24-bit unsigned, full width
    int16_t temp = (int16_t)read_be(DIETEMP, 2U, &ok);  // 16-bit signed

    // vbus: drop the 4 reserved low bits -> 20-bit unsigned. 195.3125 uV/LSB = 3125/16 uV -> mV
    sample.bus_mv = ((vbus >> 4) * 3125U) / 16000U;

    // current: sign-extend the 20-bit field, then scale by CURRENT_LSB (10 uA) -> mA
    int32_t current20 = ((int32_t)(curr << 8)) >> 12;
    sample.current_ma = (current20 * (int32_t)VAL_CURRENT_LSB) / 1000;

    // power: full 24-bit. POWER_LSB = 3.2 * CURRENT_LSB = 32 uW -> mW
    sample.power_mw = (pwr * 32U) / 1000U;

    // die temp: 7.8125 m-degC/LSB = 125/16 m-degC -> centi-degC
    sample.dietemp_cc = (int16_t)(((int32_t)temp * 125) / 160);

    sample.t_ms = millis();
    sample.valid = ok;
    return sample;
}
