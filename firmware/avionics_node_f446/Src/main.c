/**
 * @file   main.c
 * @brief  node entry - bring up the board and stream telemetry
 */

#include "app/telemetry.h"
#include "drivers/clock.h"
#include "drivers/gpio.h"
#include "drivers/systick.h"
#include "drivers/uart.h"

int main(void) {
    clock_init();
    systick_init();
    led_init();
    uart_init(115200U);

    uart_write((const uint8_t*)"uart up\r\n", 9);  // visual link check

    uint32_t tick = 0;
    for (;;) {
        led_toggle();
        telemetry_send_heartbeat();
        if (++tick % 5U == 0U) {
            telemetry_send_link_status();  // link health every 5 s
        }
        delay_ms(1000U);  // 1 hz
    }
}
