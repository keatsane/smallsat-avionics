/**
 * @file   gpio.c
 * @brief  gpio driver - ld2 led on pa5
 */

#include "drivers/gpio.h"

#include "stm32f446xx.h"

void led_init(void) {
    // ld2 on port A
    gpio_enable_port(GPIOA);

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

void gpio_enable_port(GPIO_TypeDef* port) {
    // the ports sit 0x400 apart and the ahb1enr enable bits run 0,1,2,...
    uint32_t idx = ((uint32_t)port - GPIOA_BASE) / 0x400UL;
    RCC->AHB1ENR |= (1UL << idx);
    (void)RCC->AHB1ENR;  // readback lets the clock settle before the port is touched
}

void gpio_config_af(GPIO_TypeDef* port, uint32_t pin, uint8_t af, gpio_otype_t otype,
                    gpio_speed_t speed) {
    port->MODER &= ~(0x3UL << (pin * 2U));
    port->MODER |= (0x2UL << (pin * 2U));  // alternate function

    port->OTYPER &= ~(0x1UL << pin);
    port->OTYPER |= ((uint32_t)otype << pin);

    port->OSPEEDR &= ~(0x3UL << (pin * 2U));
    port->OSPEEDR |= ((uint32_t)speed << (pin * 2U));

    port->AFR[pin >> 3U] &= ~(0xFUL << ((pin & 0x7U) * 4U));
    port->AFR[pin >> 3U] |= ((uint32_t)af << ((pin & 0x7U) * 4U));
}
