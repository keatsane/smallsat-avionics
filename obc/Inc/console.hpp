/**
 * @file   console.hpp
 * @brief  formatted text out the console uart
 *
 * Board bring-up and the control task both report over the console, so the helpers live here
 * rather than in either one. `configUSE_NEWLIB_REENTRANT` is 0, so vsnprintf is not safe from two
 * tasks at once - today only the control task calls these after the scheduler starts, and the
 * telemetry task takes ownership of all console output when it lands.
 */

#ifndef CONSOLE_HPP
#define CONSOLE_HPP

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
