/**
 * @file   pwm_dma.c
 * @brief  pwm driver - one-shot dma-fed burst on tim1_ch1, one duty value per period
 */

#include "drivers/pwm_dma.h"

#include "board.h"
#include "drivers/gpio.h"
#include "stm32f446xx.h"

#define PWM_MODE_1 6U  // ocxm = 110: high while cnt < ccr, low after

void pwm_dma_init(uint32_t period_ticks) {
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;

    gpio_enable_port(WS2812_PORT);
    gpio_config_af(WS2812_PORT, WS2812_PIN, WS2812_AF, GPIO_PUSH_PULL, GPIO_SPEED_HIGH);

    // apb2 is 90 mhz, but any prescaler other than /1 doubles the timer clock - so 180 here
    WS2812_TIM->PSC = 0U;
    WS2812_TIM->ARR = period_ticks - 1U;  // counter runs 0 to arr, so a period is arr+1 ticks

    WS2812_TIM->CCMR1 = (PWM_MODE_1 << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
    WS2812_TIM->CR1 |= TIM_CR1_ARPE;  // arr shadowed too - no tearing mid-period
    WS2812_TIM->CCER |= TIM_CCER_CC1E;
    WS2812_TIM->BDTR |= TIM_BDTR_MOE;  // advanced timer - the output stays dead without this

    WS2812_TIM->CCR1 = 0U;  // idle low - a long low is what latches a ws2812 frame

    WS2812_TIM->EGR |= TIM_EGR_UG;  // copy the preloaded arr/ccr1 into the live registers
    WS2812_TIM->CR1 |= TIM_CR1_CEN;
}

void pwm_dma_send(const uint16_t* duty, size_t n) {
    WS2812_TIM->DIER &= ~TIM_DIER_CC1DE;  // no requests while the stream is being rebuilt

    // clearing en is a request - the other registers ignore writes until it reads back low
    WS2812_STREAM->CR &= ~DMA_SxCR_EN;
    while ((WS2812_STREAM->CR & DMA_SxCR_EN) != 0U) {
    }

    // a stale flag blocks the next burst
    WS2812_DMA->LIFCR = DMA_LIFCR_CTCIF1 | DMA_LIFCR_CHTIF1 | DMA_LIFCR_CTEIF1 | DMA_LIFCR_CDMEIF1 |
                        DMA_LIFCR_CFEIF1;

    WS2812_STREAM->PAR = (uint32_t)&WS2812_TIM->CCR1;  // fixed destination
    WS2812_STREAM->M0AR = (uint32_t)duty;              // walking source
    WS2812_STREAM->NDTR = (uint32_t)n;

    WS2812_STREAM->CR = (WS2812_DMA_CH << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_DIR_0 |  // mem -> periph
                        DMA_SxCR_MINC |                       // step through the buffer
                        DMA_SxCR_PSIZE_0 | DMA_SxCR_MSIZE_0;  // 16 bits each end, like ccr1

    WS2812_STREAM->CR |= DMA_SxCR_EN;    // arm first
    WS2812_TIM->DIER |= TIM_DIER_CC1DE;  // then let the timer start asking
}

bool pwm_dma_busy(void) {
    // a non-circular stream drops en itself once ndtr hits zero
    return (WS2812_STREAM->CR & DMA_SxCR_EN) != 0U;
}
