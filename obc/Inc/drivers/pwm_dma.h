/**
 * @file   pwm_dma.h
 * @brief  one-shot pwm burst on tim1_ch1 (pa8) - the transport behind the ws2812
 */

#ifndef PWM_DMA_H
#define PWM_DMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  bring up tim1_ch1 on pa8 as a pwm output at a fixed period
 * @param  period_ticks  counter ticks per pwm period (180 mhz timer clock)
 */
void pwm_dma_init(uint32_t period_ticks);

/**
 * @brief  clock a buffer of duty values out of ccr1, one per pwm period
 * @param  duty  duty values in timer ticks - must stay valid until the burst ends
 * @param  n     how many values to send
 */
void pwm_dma_send(const uint16_t* duty, size_t n);

/**
 * @brief  is a burst still in flight
 * @return true while the dma stream is running
 */
bool pwm_dma_busy(void);

#ifdef __cplusplus
}
#endif

#endif  // PWM_DMA_H
