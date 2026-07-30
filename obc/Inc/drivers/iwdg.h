/**
 * @file   iwdg.h
 * @brief  independent watchdog - resets the board if the software stops servicing it
 *
 * Independent because it is clocked from the LSI, its own low-speed RC oscillator, not from the
 * main clock tree. A PLL that loses lock, a dead HSE, or a core wedged with interrupts masked all
 * leave the IWDG counting - which is the whole point, and why the APB-clocked WWDG would be the
 * wrong choice here (REQ-WDG-001).
 *
 * Once started it cannot be stopped. There is no disable, no pause, and no way back except a
 * reset, so nothing can quietly turn the safety net off later.
 */

#ifndef IWDG_H
#define IWDG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  start the watchdog
 * The LSI is an RC oscillator specified at 17-47 kHz, so the period cannot be exact. The reload is
 * sized against the fastest end of that spread, which makes timeout_ms a guaranteed *minimum* -
 * the actual bite lands there at worst and up to ~2.8x later on a slow part. Erring long is the
 * safe direction: a watchdog that bites early resets a healthy board.
 * @param  timeout_ms  guaranteed minimum time without a pet before the board resets
 */
void iwdg_init(uint32_t timeout_ms);

/**
 * @brief  reload the counter - "the software is still alive"
 * Call only where that claim is actually being checked. Petting unconditionally from a timer or an
 * ISR turns the watchdog into a decoration: it would then prove the interrupt still fires, not
 * that the software still works.
 */
void iwdg_pet(void);

#ifdef __cplusplus
}
#endif

#endif  // IWDG_H
