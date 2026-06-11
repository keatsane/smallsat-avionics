/**
 * @file   platform_stm32.cpp
 * @brief  stm32 backend for the flight software platform layer
 */

#include "drivers/systick.h"
#include "drivers/uart.h"
#include "fsw/platform.hpp"

namespace fsw::platform {

uint32_t now_ms() { return millis(); }

void send_telemetry(const uint8_t* frame, uint32_t len) {
    uart_write(uart_console, frame, len);
    uart_write(uart_downlink, frame, len);
}

}  // namespace fsw::platform
