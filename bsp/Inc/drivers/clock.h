/**
 * @file   clock.h
 * @brief  system clock frequencies - single source of truth for derived rates
 */

#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  refresh the cached clock values from the rcc registers (call at startup)
 */
void clock_init(void);

/**
 * @brief  core / ahb clock (hclk) in hz
 * @return hclk frequency
 */
uint32_t clock_hclk_hz(void);

/**
 * @brief  apb1 peripheral clock (pclk1) in hz
 * @return pclk1 frequency
 */
uint32_t clock_pclk1_hz(void);

/**
 * @brief  apb2 peripheral clock (pclk2) in hz
 * @return pclk2 frequency
 */
uint32_t clock_pclk2_hz(void);

#ifdef __cplusplus
}
#endif

#endif  // CLOCK_H
