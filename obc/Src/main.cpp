/**
 * @file   main.cpp
 * @brief  node entry - bring up the board, create the tasks, hand over to the scheduler
 */

#include "FreeRTOS.h"
#include "console.hpp"
#include "control_task.hpp"
#include "devices/icm20948.h"
#include "devices/ina228.h"
#include "devices/ov2640.h"
#include "devices/tmp117.h"
#include "devices/ws2812.h"
#include "drivers/clock.h"
#include "drivers/gpio.h"
#include "drivers/i2c.h"
#include "drivers/panic.h"
#include "drivers/reset.h"
#include "drivers/systick.h"
#include "drivers/uart.h"
#include "fsw/platform.hpp"
#include "sensor_task.hpp"
#include "task.h"

namespace {

bool imu_ok = false;     // did the imu answer at init - gates the sensor task's reads
bool power_ok = false;   // power monitor
bool temp_ok = false;    // temperature sensor
bool camera_ok = false;  // payload camera

}  // namespace

// bring up clocks, the time base, and peripherals; record which sensors came up
static void init(void) {
    reset_init();  // latch why we last reset before anything can trigger a new one
    clock_init();
    systick_init();
    ld2_init();
    ws2812_init();
    uart_esc_init();
    uart_console_init();   // console: usart2 -> st-link vcp (usb)
    uart_downlink_init();  // downlink: usart6 -> pc6/pc7 header

    // boot banner - why we reset + the live core clock
    console_printf("BOOT: reset=%s clk=%lu Hz\r\n", reset_cause_str(reset_cause()),
                   static_cast<unsigned long>(clock_hclk_hz()));

    // sensors take longer than the mcu to come up from cold - without this their inits silently
    // fail on power-on (a pin reset works, the rails are already stable)
    delay_ms(100U);

    imu_ok = icm20948_init();

    i2c_sensors_init();

    // bus scan - every address that acks, before any driver touches the bus.
    // expect 0x30 (camera sccb), 0x40 (ina228), 0x48 (tmp117)
    console_puts("I2C scan:");
    for (uint8_t addr = 0x08U; addr < 0x78U; addr++) {
        if (i2c_probe(i2c_sensors, addr) == I2C_OK) {
            console_printf(" 0x%02X", addr);
        }
    }
    console_puts("\r\n");

    power_ok = ina228_init();
    temp_ok = tmp117_init();

    if (!imu_ok) {
        console_puts("IMU INIT FAIL\r\n");
    } else {
        // boot gyro bias - should land near the raw zero-rate offset, not near zero
        int16_t bias[3];
        icm20948_gyro_bias(bias);
        console_printf("IMU gyro bias: %d %d %d\r\n", bias[0], bias[1], bias[2]);
    }

    if (!power_ok) {
        console_puts("POWER INIT FAIL\r\n");
    }

    if (!temp_ok) {
        console_puts("TEMP INIT FAIL\r\n");
    }

    camera_ok = ov2640_init();
    if (!camera_ok) {
        console_puts("CAMERA INIT FAIL\r\n");
    }

    // no esc check here - it spends several seconds aligning foc before it says anything.
    // the control task reports the link the moment it comes up
    fsw::platform::set_wheel_torque(0);
}

int main(void) {
    init();

    // nothing runs until the scheduler starts, so creation order is not a startup order - each
    // task's own file owns its stack, its priority, and whatever it needs to be told at init
    control_task_create(camera_ok);
    sensor_task_create(imu_ok, power_ok, temp_ok);

    systick_kernel_tick_enable();  // hand the tick over before anything expects to be scheduled
    vTaskStartScheduler();

    // only reached if the scheduler could not start, which with static allocation means the idle
    // task's storage was refused - a build problem, not a runtime one
    panic_puts("\r\nSCHEDULER FAILED TO START\r\n");
    panic_drain();
    NVIC_SystemReset();
    for (;;) {
    }
}
