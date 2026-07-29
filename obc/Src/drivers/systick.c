/**
 * @file   systick.c
 * @brief  cortex-m systick driver - 1 ms tick and blocking delay
 */

#include "drivers/systick.h"

#include "stm32f446xx.h"

#define TICK_HZ 1000U  // 1 ms tick

static volatile uint32_t s_ticks;

void systick_init(void) {
    // reload from the live core clock
    SysTick_Config(SystemCoreClock / TICK_HZ);
}

// overrides the weak vector in startup_stm32f446retx.s
void SysTick_Handler(void) { s_ticks++; }

uint32_t millis(void) { return s_ticks; }

void delay_ms(uint32_t ms) {
    uint32_t start = s_ticks;
    while ((s_ticks - start) < ms) {
        __WFI();
    }
}
