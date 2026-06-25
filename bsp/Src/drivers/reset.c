/**
 * @file   reset.c
 * @brief  decode and latch the rcc reset flags so boot can report why we reset
 */

#include "drivers/reset.h"

#include "stm32f446xx.h"

static reset_cause_t s_cause = RESET_CAUSE_UNKNOWN;

void reset_init(void) {
    uint32_t csr = RCC->CSR;

    // priority order: one reset sets several flags on the f4 (a clean power-up
    // raises por AND bor AND pin), so test the most specific cause first.
    // por before bor - a power-up sets both and power-on is the real story; bor
    // set without por is the genuine sag-and-recover brownout
    if (csr & RCC_CSR_IWDGRSTF) {
        s_cause = RESET_CAUSE_IWDG;
    } else if (csr & RCC_CSR_WWDGRSTF) {
        s_cause = RESET_CAUSE_WWDG;
    } else if (csr & RCC_CSR_LPWRRSTF) {
        s_cause = RESET_CAUSE_LOWPOWER;
    } else if (csr & RCC_CSR_SFTRSTF) {
        s_cause = RESET_CAUSE_SOFTWARE;
    } else if (csr & RCC_CSR_PORRSTF) {
        s_cause = RESET_CAUSE_POWER_ON;
    } else if (csr & RCC_CSR_BORRSTF) {
        s_cause = RESET_CAUSE_BROWNOUT;
    } else if (csr & RCC_CSR_PINRSTF) {
        s_cause = RESET_CAUSE_PIN;
    } else {
        s_cause = RESET_CAUSE_UNKNOWN;
    }

    // clear the flags so the next reset reads clean - they latch until rmvf
    RCC->CSR |= RCC_CSR_RMVF;
}

reset_cause_t reset_cause(void) { return s_cause; }

const char* reset_cause_str(reset_cause_t c) {
    switch (c) {
        case RESET_CAUSE_POWER_ON:
            return "power-on";
        case RESET_CAUSE_PIN:
            return "pin";
        case RESET_CAUSE_BROWNOUT:
            return "brownout";
        case RESET_CAUSE_SOFTWARE:
            return "software";
        case RESET_CAUSE_IWDG:
            return "iwdg-watchdog";
        case RESET_CAUSE_WWDG:
            return "wwdg-watchdog";
        case RESET_CAUSE_LOWPOWER:
            return "low-power";
        default:
            return "unknown";
    }
}
