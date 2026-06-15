/**
 * @file   gpio.h
 * @brief  gpio driver - ld2 led on pa5
 */

#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

#include "stm32f446xx.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  enable gpioa and drive pa5 (ld2) as a push-pull output
 */
void led_init(void);

/**
 * @brief  toggle pa5 (ld2) output data register
 */
void led_toggle(void);

// alternate-function pin setup, shared by the peripheral drivers

typedef enum {
    GPIO_PUSH_PULL = 0,
    GPIO_OPEN_DRAIN = 1,
} gpio_otype_t;

typedef enum {
    GPIO_SPEED_LOW = 0,
    GPIO_SPEED_MEDIUM = 1,
    GPIO_SPEED_HIGH = 2,
    GPIO_SPEED_VERY_HIGH = 3,
} gpio_speed_t;

/**
 * @brief  enable a gpio port's ahb1 clock - call before configuring its pins
 * @param  port  the gpio port (GPIOA, GPIOB, ...)
 */
void gpio_enable_port(GPIO_TypeDef* port);

/**
 * @brief  configure one pin as an alternate function
 * @param  port   the gpio port
 * @param  pin    pin number, 0-15
 * @param  af     alternate-function number, 0-15 (see the datasheet af table)
 * @param  otype  push-pull or open-drain
 * @param  speed  output slew-rate setting
 */
void gpio_config_af(GPIO_TypeDef* port, uint32_t pin, uint8_t af, gpio_otype_t otype,
                    gpio_speed_t speed);

#ifdef __cplusplus
}
#endif

#endif  // GPIO_H
