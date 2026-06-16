/**
 * @file   gpio.c
 * @brief  gpio driver - ld2 led on pa5
 */

#include "drivers/gpio.h"

#include "board.h"
#include "stm32f446xx.h"

void led_init(void) {
    // ld2
    gpio_enable_port(LED_PORT);

    // led pin as output
    LED_PORT->MODER &= ~(0x3UL << (LED_PIN * 2U));
    LED_PORT->MODER |= (0x1UL << (LED_PIN * 2U));
}

void led_toggle(void) {
    // drive via bsrr (low half sets, high half resets) - the write is atomic, so an isr
    // touching another port-a pin can't clobber a read-modify-write on the whole odr
    if (LED_PORT->ODR & (1U << LED_PIN)) {
        LED_PORT->BSRR = (1U << (LED_PIN + 16U));  // currently high -> reset
    } else {
        LED_PORT->BSRR = (1U << LED_PIN);  // currently low -> set
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
