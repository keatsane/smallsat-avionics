/**
 * @file   main.cpp
 * @brief  node entry - bring up the board, then run the flight-software cycle
 */

#include "devices/icm20948.h"
#include "devices/ina228.h"
#include "devices/tmp117.h"
#include "drivers/clock.h"
#include "drivers/gpio.h"
#include "drivers/i2c.h"
#include "drivers/systick.h"
#include "drivers/uart.h"
#include "fsw/executive.hpp"

namespace {

fsw::Executive exec;
bool imu_ok = false;    // did the imu answer at init - gates reads below
bool power_ok = false;  // power monitor
bool temp_ok = false;   // temperature sensor

}  // namespace

// bring up clocks, the time base, and peripherals; record which sensors came up
static void init(void) {
    clock_init();
    systick_init();
    led_init();
    uart_console_init();   // console: usart2 -> st-link vcp (usb)
    uart_downlink_init();  // downlink: usart6 -> pc6/pc7 header

    imu_ok = icm20948_init();

    i2c_sensors_init();

    power_ok = ina228_init();
    temp_ok = tmp117_init();

    if (!imu_ok) {
        uart_write(uart_console, reinterpret_cast<const uint8_t*>("IMU INIT FAIL\r\n"), 15U);
    }

    if (!power_ok) {
        uart_write(uart_console, reinterpret_cast<const uint8_t*>("POWER INIT FAIL\r\n"), 17U);
    }

    if (!temp_ok) {
        uart_write(uart_console, reinterpret_cast<const uint8_t*>("TEMP INIT FAIL\r\n"), 16U);
    }
}

// raw driver reading -> wire/telemetry form (the one place the bsp and fsw types meet)
static fsw::imu_data_t to_imu_data(const icm20948_sample_t& s) {
    fsw::imu_data_t d{};
    d.t_ms = s.t_ms;
    for (size_t i = 0U; i < 3U; i++) {
        d.accel[i] = s.accel[i];
        d.gyro[i] = s.gyro[i];
        d.mag[i] = s.mag[i];
    }
    d.flags = static_cast<uint8_t>((s.accel_gyro_valid ? fsw::kImuFlagAccelGyroValid : 0U) |
                                   (s.mag_valid ? fsw::kImuFlagMagValid : 0U));
    return d;
}

static fsw::power_data_t to_power_data(const ina228_sample_t& s) {
    fsw::power_data_t d{};
    d.t_ms = s.t_ms;
    d.bus_mv = s.bus_mv;
    d.current_ma = s.current_ma;
    d.power_mw = s.power_mw;
    d.flags = static_cast<uint8_t>(s.valid ? fsw::kPowerFlagValid : 0U);
    return d;
}

static fsw::temp_data_t to_temp_data(const tmp117_sample_t& s) {
    fsw::temp_data_t d{};
    d.t_ms = s.t_ms;
    d.temp_mc = s.temp_mc;
    d.flags = static_cast<uint8_t>(s.valid ? fsw::kTempFlagValid : 0U);
    return d;
}

// read every available sensor into this cycle's inputs
static void read_sensors(fsw::Inputs& inputs) {
    // always hand the fsw a sample so the sensor monitor can judge it, defaults to empty (invalid)
    inputs.imu = imu_ok ? to_imu_data(icm20948_read()) : fsw::imu_data_t{};
    inputs.power = power_ok ? to_power_data(ina228_read()) : fsw::power_data_t{};
    inputs.temp = temp_ok ? to_temp_data(tmp117_read()) : fsw::temp_data_t{};

    // ...
}

int main(void) {
    init();

    uint32_t cycle = 0U;
    for (;;) {
        fsw::Inputs inputs{};
        read_sensors(inputs);
        exec.cycle(inputs, millis());

        if ((cycle % 10U) == 0U) {
            led_toggle();  // ~1 hz alive blink
        }

        cycle++;
        delay_ms(100U);  // 10 hz cycle
    }
}
