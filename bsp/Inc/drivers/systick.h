/**
 * @file   systick.h
 * @brief  1 khz systick time base - ms tick and blocking delay
 */

#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  start systick at 1 khz (1 ms tick)
 * derives the reload from SystemCoreClock, so call SystemCoreClockUpdate()
 * after any clock-tree change before calling this
 */
void systick_init(void);

/**
 * @brief  milliseconds elapsed since systick_init
 * @return ms tick count (wraps after ~49 days)
 */
uint32_t millis(void);

/**
 * @brief  busy-wait for a number of milliseconds
 * @param  ms delay length in milliseconds
 */
void delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif  // SYSTICK_H
