/**
 * @file   console.cpp
 * @brief  formatted text out the console uart
 */

#include "console.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "drivers/uart.h"

void console_puts(const char* s) {
    uart_write(uart_console, reinterpret_cast<const uint8_t*>(s), std::strlen(s));
}

void console_printf(const char* fmt, ...) {
    char line[96];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    // vsnprintf returns what it wanted to write, not what fit
    size_t len = static_cast<size_t>(n);
    if (len >= sizeof(line)) {
        len = sizeof(line) - 1U;
    }
    uart_write(uart_console, reinterpret_cast<const uint8_t*>(line), len);
}
