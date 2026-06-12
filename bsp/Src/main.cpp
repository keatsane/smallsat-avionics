/**
 * @file   main.cpp
 * @brief  node entry - bring up the board
 */

#include "drivers/clock.h"
#include "drivers/gpio.h"
#include "drivers/imu.h"
#include "drivers/systick.h"
#include "drivers/uart.h"
#include "fsw/executive.hpp"

#define UART_BAUD 115200U  // console + downlink run the same rate for now

namespace {

fsw::Executive exec;

}  // namespace

// bring-up only: print a signed int right-aligned in a 7-char field
static void print_i16(int32_t v) {
    char buf[8] = "       ";
    bool neg = v < 0;
    if (neg) v = -v;
    size_t i = sizeof(buf) - 1;
    do {
        buf[--i] = '0' + (v % 10);
        v /= 10;
    } while (v && i);
    if (neg && i) buf[--i] = '-';
    uart_write(uart_console, (const uint8_t*)buf, sizeof(buf) - 1);
}

int main(void) {
    clock_init();
    systick_init();
    led_init();
    uart_console_init(UART_BAUD);   // console: usart2 -> st-link vcp (usb)
    uart_downlink_init(UART_BAUD);  // downlink: usart6 -> pc6/pc7 header

    bool imu_ok = imu_init();
    if (!imu_ok) {
        uart_write(uart_console, (const uint8_t*)"IMU INIT FAIL\r\n", 15U);
    }

    uint32_t cycle = 0U;
    for (;;) {
        fsw::Inputs inputs{};
        exec.cycle(inputs, millis());

        if (((cycle % 10U) == 0U) && imu_ok) {
            led_toggle();

            imu_sample_t s;
            imu_read(&s);
            uart_write(uart_console, (const uint8_t*)"ACCEL: ", 7U);
            print_i16(s.accel[0]);
            print_i16(s.accel[1]);
            print_i16(s.accel[2]);
            uart_write(uart_console, (const uint8_t*)", GYRO: ", 8U);
            print_i16(s.gyro[0]);
            print_i16(s.gyro[1]);
            print_i16(s.gyro[2]);
            uart_write(uart_console, (const uint8_t*)"\r\n", 2U);
        }

        cycle++;
        delay_ms(100U);
    }
}
