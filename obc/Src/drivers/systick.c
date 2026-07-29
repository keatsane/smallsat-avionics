/**
 * @file   systick.c
 * @brief  cortex-m systick driver - 1 ms tick and blocking delay
 */

#include "drivers/systick.h"

#include "stm32f446xx.h"

// the port declares this privately in port.c - the only thing that normally reaches it is the
// xPortSysTickHandler -> SysTick_Handler mapping in FreeRTOSConfig.h, which this file replaces.
// declared here rather than including the kernel headers, so the driver stays free of them
extern void xPortSysTickHandler(void);

#define TICK_HZ 1000U  // 1 ms tick

static volatile uint32_t s_ticks;
static volatile uint8_t s_kernel_tick;  // once set, every tick is also handed to the scheduler

void systick_init(void) {
    // reload from the live core clock
    SysTick_Config(SystemCoreClock / TICK_HZ);
}

// the kernel wants this vector too, and both need it: the scheduler cannot run without a tick and
// the fsw timestamps everything off millis(), which must not restart at zero when the scheduler
// starts. so the driver keeps the vector and forwards. the flag is what makes it safe - calling
// xPortSysTickHandler before vTaskStartScheduler touches a scheduler that does not exist yet.
// note the kernel programs the same 1 ms period (configTICK_RATE_HZ), so s_ticks keeps counting ms
void systick_kernel_tick_enable(void) { s_kernel_tick = 1U; }

// overrides the weak vector in startup_stm32f446retx.s
void SysTick_Handler(void) {
    s_ticks++;
    if (s_kernel_tick != 0U) {
        xPortSysTickHandler();
    }
}

uint32_t millis(void) { return s_ticks; }

void delay_ms(uint32_t ms) {
    uint32_t start = s_ticks;
    while ((s_ticks - start) < ms) {
        __WFI();
    }
}
