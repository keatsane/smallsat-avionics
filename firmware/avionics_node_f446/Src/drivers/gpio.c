/**
 * @file   gpio.c
 * @brief  gpio driver - ld2 led on pa5
 */

#include "drivers/gpio.h"

#include "stm32f446xx.h"

void led_init(void) {
    // ld2 on port A
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;

    // set mode to output for pa5
    GPIOA->MODER &= ~GPIO_MODER_MODE5_Msk;
    GPIOA->MODER |= GPIO_MODER_MODE5_0;
}

void led_toggle(void) {
    // toggle pa5
    GPIOA->ODR ^= GPIO_ODR_ODR_5;
}
