/**
 * @file   panic.h
 * @brief  polled console writes for the paths where the uart driver cannot run
 *
 * The normal console is interrupt-driven, so it needs its tx isr to make progress. Anything that
 * runs with interrupts masked at or above the isr's priority - a fault handler, a kernel hook -
 * would block forever waiting for a ring slot. These push bytes straight at the usart instead.
 */

#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  write a string to the console, polling txe for each byte
 * @param  s nul-terminated string
 */
void panic_puts(const char* s);

/**
 * @brief  write a 32-bit value as 0x-prefixed lowercase hex
 * @param  v value to print
 */
void panic_hex(uint32_t v);

/**
 * @brief  wait until the last byte has cleared the wire
 * call before resetting, or the message is lost in the shift register
 */
void panic_drain(void);

#ifdef __cplusplus
}
#endif

#endif  // PANIC_H
