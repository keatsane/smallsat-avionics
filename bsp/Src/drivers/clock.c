/**
 * @file   clock.c
 * @brief  system clock bring-up (8 mhz hse -> pll -> 180 mhz) + derived rates
 */

#include "drivers/clock.h"

#include "stm32f446xx.h"

// nucleo 8 mhz hse (st-link mco, bypass) -> 180 mhz sysclk:
// vco_in = hse / M = 8/4 = 2 mhz, vco = vco_in * N = 360 mhz, sysclk = vco / P = 180 mhz
#define PLL_M     4U
#define PLL_N     180U
#define PLL_P     2U  // divides the vco down to sysclk; encoded as (P/2 - 1) in pllcfgr
#define PLL_Q     7U  // feeds usb/sdio/rng - unused here, just kept in range
#define SYSCLK_HZ 180000000U

void clock_init(void) {
    // bring the f446 from its 16 mhz hsi reset default up to 180 mhz

    // the PWR peripheral does the voltage scaling
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;

    // voltage scale 1 - the regulator's top setting
    PWR->CR |= PWR_CR_VOS;

    // start the hse: the nucleo has no crystal - hse is an 8 mhz square wave fed
    // in from the st-link, so it runs in BYPASS mode, not as an oscillator
    RCC->CR |= RCC_CR_HSEBYP;
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
    }

    // over-drive needed to get to 180 MHz from 168 MHz scale 1 cap
    PWR->CR |= PWR_CR_ODEN;
    while ((PWR->CSR & PWR_CSR_ODRDY) == 0U) {
    }
    PWR->CR |= PWR_CR_ODSWEN;
    while ((PWR->CSR & PWR_CSR_ODSWRDY) == 0U) {
    }

    // flash wait states before speed up - at 180 mhz the flash needs 5
    FLASH->ACR = FLASH_ACR_LATENCY_5WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    // configure + start the pll (pllcfgr is only writable while the pll is off)
    RCC->PLLCFGR = (PLL_M << RCC_PLLCFGR_PLLM_Pos) | (PLL_N << RCC_PLLCFGR_PLLN_Pos) |
                   (((PLL_P >> 1U) - 1U) << RCC_PLLCFGR_PLLP_Pos) | RCC_PLLCFGR_PLLSRC_HSE |
                   (PLL_Q << RCC_PLLCFGR_PLLQ_Pos);
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {
    }

    // divide the buses BEFORE the switch - when sysclk jumps to 180, apb1 must
    // already be <= 45 mhz and apb2 <= 90 mhz, or they'd be over spec
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 |   // ahb  = 180 mhz
                 RCC_CFGR_PPRE1_DIV4 |  // apb1 = 45 mhz
                 RCC_CFGR_PPRE2_DIV2;   // apb2 = 90 mhz

    // hand the system clock to the pll and wait for the hardware to confirm
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }

    // set the cmsis clock variable directly
    SystemCoreClock = SYSCLK_HZ;
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
