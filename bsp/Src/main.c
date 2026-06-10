/**
 * @file   main.c
 * @brief  node entry - bring up the board
 */

// #include "../../fsw/include/fsw/executive.hpp"
#include "drivers/clock.h"
#include "drivers/gpio.h"
#include "drivers/systick.h"
#include "drivers/uart.h"

#define UART_BAUD 115200U  // console + downlink run the same rate for now

int main(void) {
    clock_init();
    systick_init();
    led_init();
    uart_console_init(UART_BAUD);   // console: usart2 -> st-link vcp (usb)
    uart_downlink_init(UART_BAUD);  // downlink: usart6 -> pc6/pc7 header

    uart_write(uart_console, (const uint8_t*)"uart up\r\n", 9);  // visual link check

    for (;;) {
        led_toggle();
        // cycle(millis());
        delay_ms(1000U);  // 1 hz heartbeat blink
    }
}
