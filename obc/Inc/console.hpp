/**
 * @file   console.hpp
 * @brief  formatted text out the console uart
 *
 * Board bring-up and the control task both report over the console, so the helpers live here
 * rather than in either one. Output goes to the telemetry task, which owns the uart; these never
 * block on the wire. `configUSE_NEWLIB_REENTRANT` is 0, so vsnprintf is not safe from two tasks at
 * once and console_printf holds a lock across the format - see console_lock_init.
 */

#ifndef CONSOLE_HPP
#define CONSOLE_HPP

/**
 * @brief  create the formatting lock
 * Same reasoning as uart_locks_init: board bring-up must not call into the kernel, so this is
 * separate and runs once the board is up and before the scheduler starts. Until then there is one
 * thread of execution and console_printf serialises nothing, which is correct.
 */
void console_lock_init(void);

/**
 * @brief  write a nul-terminated string, length taken from the string
 * @param  s the string
 */
void console_puts(const char* s);

/**
 * @brief  printf to the console, truncated to a fixed line buffer
 * @param  fmt printf-style format string
 */
void console_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#endif  // CONSOLE_HPP
