/**
 * @file   console.cpp
 * @brief  formatted text out the console uart
 */

#include "console.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "telemetry_task.hpp"

namespace {

// vsnprintf writes into a caller buffer but still reaches newlib's shared reentrancy state, and
// configUSE_NEWLIB_REENTRANT is 0 - there is one copy of that state for the whole system. the line
// buffer below is per-call, so this lock is only about newlib
SemaphoreHandle_t s_fmt_lock;
StaticSemaphore_t s_fmt_lock_buf;

bool fmt_lock(void) {
    if (s_fmt_lock == nullptr || xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return false;
    }
    return xSemaphoreTake(s_fmt_lock, portMAX_DELAY) == pdTRUE;
}

}  // namespace

void console_lock_init(void) { s_fmt_lock = xSemaphoreCreateMutexStatic(&s_fmt_lock_buf); }

void console_puts(const char* s) {
    // no formatting, so no lock needed - the enqueue is atomic on its own
    (void)telemetry_out_console(reinterpret_cast<const uint8_t*>(s), std::strlen(s));
}

void console_printf(const char* fmt, ...) {
    char line[96];

    const bool locked = fmt_lock();

    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (locked) {
        (void)xSemaphoreGive(s_fmt_lock);
    }

    if (n <= 0) {
        return;
    }
    // vsnprintf returns what it wanted to write, not what fit
    size_t len = static_cast<size_t>(n);
    if (len >= sizeof(line)) {
        len = sizeof(line) - 1U;
    }
    (void)telemetry_out_console(reinterpret_cast<const uint8_t*>(line), len);
}
