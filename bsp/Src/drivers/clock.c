/**
 * @file   clock.c
 * @brief  system clock frequencies derived from the rcc registers
 */

#include "drivers/clock.h"

#include "stm32f446xx.h"

void clock_init(void) {
    // running on the 16 mhz hsi reset default; pll bring-up would go here
    SystemCoreClockUpdate();
}

uint32_t clock_hclk_hz(void) { return SystemCoreClock; }

uint32_t clock_pclk1_hz(void) {
    // pclk1 = hclk divided by the apb1 prescaler
    uint32_t ppre1 = (RCC->CFGR & RCC_CFGR_PPRE1) >> RCC_CFGR_PPRE1_Pos;
    return SystemCoreClock >> APBPrescTable[ppre1];
}

uint32_t clock_pclk2_hz(void) {
    // pclk2 = hclk divided by the apb2 prescaler
    uint32_t ppre2 = (RCC->CFGR & RCC_CFGR_PPRE2) >> RCC_CFGR_PPRE2_Pos;
    return SystemCoreClock >> APBPrescTable[ppre2];
}
