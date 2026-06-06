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
    // drive via bsrr (low half sets, high half resets) - the write is atomic, so an isr
    // touching another port-a pin can't clobber a read-modify-write on the whole odr
    if (GPIOA->ODR & GPIO_ODR_ODR_5) {
        GPIOA->BSRR = (1U << (5U + 16U));  // currently high -> reset pa5
    } else {
        GPIOA->BSRR = (1U << 5U);  // currently low -> set pa5
    }
}
