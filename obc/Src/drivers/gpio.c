/**
 * @file   gpio.c
 * @brief  gpio driver - ld2 on pa5, plus the port/pin setup the other drivers share
 */

#include "drivers/gpio.h"

#include "board.h"
#include "stm32f446xx.h"

void ld2_init(void) {
    gpio_enable_port(LD2_PORT);
    gpio_config_output(LD2_PORT, LD2_PIN);
}

void ld2_toggle(void) {
    // drive via bsrr (low half sets, high half resets) - the write is atomic, so an isr
    // touching another port-a pin can't clobber a read-modify-write on the whole odr
    if (LD2_PORT->ODR & (1U << LD2_PIN)) {
        LD2_PORT->BSRR = (1U << (LD2_PIN + 16U));  // currently high -> reset
    } else {
        LD2_PORT->BSRR = (1U << LD2_PIN);  // currently low -> set
    }
}

void gpio_enable_port(GPIO_TypeDef* port) {
    // the ports sit 0x400 apart and the ahb1enr enable bits run 0,1,2,...
    uint32_t idx = ((uint32_t)port - GPIOA_BASE) / 0x400UL;
    RCC->AHB1ENR |= (1UL << idx);
    (void)RCC->AHB1ENR;  // readback lets the clock settle before the port is touched
}

void gpio_config_output(GPIO_TypeDef* port, uint32_t pin) {
    port->MODER &= ~(0x3UL << (pin * 2U));
    port->MODER |= (0x1UL << (pin * 2U));
}

void gpio_config_input(GPIO_TypeDef* port, uint32_t pin) {
    // mode 00 is input and is also the reset value for most pins - set explicitly anyway, since
    // "it happens to be right after reset" stops being true the moment a pin is reused
    port->MODER &= ~(0x3UL << (pin * 2U));
    port->PUPDR &= ~(0x3UL << (pin * 2U));  // no pull - the radio drives dio0 both ways
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
