/**
 * @file   gpio.h
 * @brief  gpio driver - ld2 led on pa5
 */

#ifndef GPIO_H
#define GPIO_H

/**
 * @brief  enable gpioa and drive pa5 (ld2) as a push-pull output
 */
void led_init(void);

/**
 * @brief  toggle pa5 (ld2) output data register
 */
void led_toggle(void);

#endif  // GPIO_H
