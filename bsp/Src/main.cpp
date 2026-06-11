/**
 * @file   main.cpp
 * @brief  node entry - bring up the board
 */

#include "drivers/clock.h"
#include "drivers/gpio.h"
#include "drivers/systick.h"
#include "drivers/uart.h"
#include "fsw/executive.hpp"

#define UART_BAUD 115200U  // console + downlink run the same rate for now

namespace {

fsw::Executive exec;

}  // namespace

int main(void) {
    clock_init();
    systick_init();
    led_init();
    uart_console_init(UART_BAUD);   // console: usart2 -> st-link vcp (usb)
    uart_downlink_init(UART_BAUD);  // downlink: usart6 -> pc6/pc7 header

    for (;;) {
        fsw::Inputs inputs{};
        exec.cycle(inputs, millis());
        led_toggle();
        delay_ms(100U);
    }
}
