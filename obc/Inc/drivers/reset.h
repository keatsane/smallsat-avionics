/**
 * @file   reset.h
 * @brief  why the mcu last reset - latched from the rcc reset flags at boot
 */

#ifndef RESET_H
#define RESET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// why the last reset happened, decoded from rcc->csr
typedef enum {
    RESET_CAUSE_UNKNOWN = 0,
    RESET_CAUSE_POWER_ON,  // por/pdr - cold power-up
    RESET_CAUSE_PIN,       // nrst pin pulled low (the black button)
    RESET_CAUSE_BROWNOUT,  // supply dipped below the bor threshold
    RESET_CAUSE_SOFTWARE,  // NVIC_SystemReset / SYSRESETREQ
    RESET_CAUSE_IWDG,      // independent watchdog timed out
    RESET_CAUSE_WWDG,      // window watchdog
    RESET_CAUSE_LOWPOWER,  // illegal low-power state
} reset_cause_t;

/**
 * @brief  latch the reset cause from rcc->csr, then clear the flags
 * @note   call once, first thing in boot, before anything can trigger a reset
 */
void reset_init(void);

/**
 * @brief  the cause latched at boot
 * @return reset_cause_t
 */
reset_cause_t reset_cause(void);

/**
 * @brief  short human label for a reset cause
 * @param  c reset cause
 * @return static string, never null
 */
const char* reset_cause_str(reset_cause_t c);

#ifdef __cplusplus
}
#endif

#endif  // RESET_H
