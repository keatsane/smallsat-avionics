/**
 * @file   uart.c
 * @brief  uart driver - serial i/o
 */

#include "drivers/uart.h"

#include "drivers/clock.h"
#include "stm32f446xx.h"

#define BUF_SIZE 256U  // power of 2

static volatile uint8_t tx_buf[BUF_SIZE];
static volatile uint16_t tx_head;  // producer: uart_write
static volatile uint16_t tx_tail;  // consumer: isr

static volatile uint8_t rx_buf[BUF_SIZE];
static volatile uint16_t rx_head;  // producer: isr
static volatile uint16_t rx_tail;  // consumer: uart_read_byte

static volatile uart_errors_t rx_errors;  // receive-line error counts

void uart_init(uint32_t baud) {
    // usart2 wired to st-link vcp (usb)
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB1ENR;

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;

    // enable alternate function 7 on pa2/3
    GPIOA->MODER &= ~(GPIO_MODER_MODE2_Msk | GPIO_MODER_MODE3_Msk);
    GPIOA->MODER |= (GPIO_MODER_MODE2_1 | GPIO_MODER_MODE3_1);
    GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL2_Msk | GPIO_AFRL_AFSEL3_Msk);
    GPIOA->AFR[0] |= (0x7UL << GPIO_AFRL_AFSEL2_Pos) | (0x7UL << GPIO_AFRL_AFSEL3_Pos);

    // baud derived from the live apb1 clock, not a hardcoded assumption
    uint32_t pclk1 = clock_pclk1_hz();
    USART2->BRR = (pclk1 + baud / 2U) / baud;

    // enable tx/rx and usart
    USART2->CR1 |= (USART_CR1_TE | USART_CR1_RE);
    USART2->CR1 |= USART_CR1_UE;

    // rx interrupt is always-on; tx interrupt is armed on demand in uart_write
    USART2->CR1 |= USART_CR1_RXNEIE;
    NVIC_EnableIRQ(USART2_IRQn);
}

void uart_write(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint16_t next = (uint16_t)((tx_head + 1U) & (BUF_SIZE - 1U));
        while (next == tx_tail) {
            // tx buffer full - wait for the isr to make room
        }
        tx_buf[tx_head] = data[i];
        tx_head = next;
        USART2->CR1 |= USART_CR1_TXEIE;  // kick the tx interrupt
    }
}

bool uart_tx_done(void) { return (tx_head == tx_tail) && (USART2->SR & USART_SR_TC); }

void uart_flush(void) {
    while (!uart_tx_done()) {
    }
}

bool uart_read_byte(uint8_t* out) {
    if (rx_head == rx_tail) {
        return false;
    }
    *out = rx_buf[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) & (BUF_SIZE - 1U));
    return true;
}

size_t uart_rx_available(void) { return (size_t)((rx_head - rx_tail) & (BUF_SIZE - 1U)); }

void uart_get_errors(uart_errors_t* out) {
    out->overrun = rx_errors.overrun;
    out->framing = rx_errors.framing;
    out->noise = rx_errors.noise;
    out->dropped = rx_errors.dropped;
}

// overrides the weak vector in startup_stm32f446retx.s
void USART2_IRQHandler(void) {
    uint32_t sr = USART2->SR;

    // receive-line errors - cleared together with rxne by the dr read below
    if (sr & USART_SR_ORE) {
        rx_errors.overrun++;
    }
    if (sr & USART_SR_FE) {
        rx_errors.framing++;
    }
    if (sr & USART_SR_NE) {
        rx_errors.noise++;
    }

    // byte arrived (reading dr clears rxne, and any pending ore/fe/ne)
    if (sr & USART_SR_RXNE) {
        uint8_t b = (uint8_t)USART2->DR;
        uint16_t next = (uint16_t)((rx_head + 1U) & (BUF_SIZE - 1U));
        if (next != rx_tail) {
            rx_buf[rx_head] = b;
            rx_head = next;
        } else {
            rx_errors.dropped++;  // rx buffer full - byte discarded
        }
    }

    // ready for next tx byte
    if ((sr & USART_SR_TXE) && (USART2->CR1 & USART_CR1_TXEIE)) {
        if (tx_head != tx_tail) {
            USART2->DR = tx_buf[tx_tail];
            tx_tail = (uint16_t)((tx_tail + 1U) & (BUF_SIZE - 1U));
        }
        if (tx_head == tx_tail) {
            USART2->CR1 &= ~USART_CR1_TXEIE;  // buffer empty - disable or it storms
        }
    }
}
