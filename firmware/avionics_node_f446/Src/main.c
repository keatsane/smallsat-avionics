/**
 * @file   main.c
 * @brief  bare-metal LED blink on the Nucleo-F446RE (LD2 / PA5)
 */

#include "stm32f446xx.h"

#define LED_PIN 5U  // LD2

static void led_init(void);
static void delay(volatile uint32_t loops);

int main(void) {
  led_init();
  for (;;) {
    GPIOA->ODR ^= (1U << LED_PIN);
    delay(400000U);
  }
}

static void led_init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
  (void)RCC->AHB1ENR;  // let the clock settle
  GPIOA->MODER &= ~(3U << (LED_PIN * 2U));
  GPIOA->MODER |= (1U << (LED_PIN * 2U));  // pa5 output
}

// crude busy-wait, swap for systick later
static void delay(volatile uint32_t loops) {
  while (loops-- != 0U) {
    __asm volatile("nop");
  }
}
