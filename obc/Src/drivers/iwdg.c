/**
 * @file   iwdg.c
 * @brief  independent watchdog - resets the board if the software stops servicing it
 */

#include "drivers/iwdg.h"

#include "stm32f446xx.h"

// key register commands (rm0390 21.4.1)
#define IWDG_KEY_RELOAD 0xAAAAU  // reload the counter from RLR - the pet
#define IWDG_KEY_UNLOCK 0x5555U  // allow writes to PR and RLR
#define IWDG_KEY_START  0xCCCCU  // start counting; irreversible until reset

// the lsi is an rc oscillator, not a crystal: 32 khz nominal, 17-47 khz over the full range
// (ds10693 table 37). sizing against the fast end is what makes the requested timeout a floor
#define LSI_MAX_HZ 47000U

#define IWDG_RLR_MAX 0xFFFU  // 12-bit reload

void iwdg_init(uint32_t timeout_ms) {
    // find the smallest prescaler whose reload fits, so the counter keeps as much resolution as
    // it can. PR encodes the divider as 4 << pr: 0 -> /4, 1 -> /8, ... 6 -> /256
    uint32_t pr = 0U;
    uint32_t reload = 0U;
    for (; pr <= 6U; pr++) {
        const uint32_t divider = 4U << pr;
        reload = (timeout_ms * (LSI_MAX_HZ / 1000U)) / divider;
        if (reload <= IWDG_RLR_MAX) {
            break;
        }
    }
    if (pr > 6U) {  // longer than the hardware can count even at /256 - clamp to the longest
        pr = 6U;
        reload = IWDG_RLR_MAX;
    }
    if (reload == 0U) {
        reload = 1U;  // a zero reload would bite immediately and forever
    }

    IWDG->KR = IWDG_KEY_START;
    IWDG->KR = IWDG_KEY_UNLOCK;

    // PR and RLR are in the lsi clock domain, so each write takes time to land and the status bits
    // say when. writing while one is pending is ignored, which would leave the default period
    while ((IWDG->SR & IWDG_SR_PVU) != 0U) {
    }
    IWDG->PR = pr;

    while ((IWDG->SR & IWDG_SR_RVU) != 0U) {
    }
    IWDG->RLR = reload;

    IWDG->KR = IWDG_KEY_RELOAD;  // load the counter with the value just programmed
}

void iwdg_pet(void) { IWDG->KR = IWDG_KEY_RELOAD; }
