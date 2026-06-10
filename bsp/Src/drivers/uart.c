/**
 * @file   uart.c
 * @brief  uart driver - interrupt-driven tx + rx, shared across usart instances
 */

#include "drivers/uart.h"

#include "drivers/clock.h"
#include "stm32f446xx.h"

#define BUF_SIZE 256U  // power of 2 - the wrap masks below depend on it
_Static_assert((BUF_SIZE & (BUF_SIZE - 1U)) == 0U, "BUF_SIZE must be a power of 2");

// per-instance state: the register block plus this uart's own ring buffers
struct uart {
    USART_TypeDef* regs;

    volatile uint8_t tx_buf[BUF_SIZE];
    volatile uint16_t tx_head;  // producer: uart_write
    volatile uint16_t tx_tail;  // consumer: isr

    volatile uint8_t rx_buf[BUF_SIZE];
    volatile uint16_t rx_head;  // producer: isr
    volatile uint16_t rx_tail;  // consumer: uart_read_byte

    volatile uart_errors_t errors;  // receive-line error counts
};

static struct uart console_inst = {.regs = USART2};
static struct uart downlink_inst = {.regs = USART6};

uart_t* const uart_console = &console_inst;
uart_t* const uart_downlink = &downlink_inst;

// the part that's identical for every usart: baud, enable tx/rx, arm rx interrupt
static void uart_start(uart_t* u, uint32_t pclk_hz, IRQn_Type irqn, uint32_t baud) {
    u->regs->BRR = (pclk_hz + baud / 2U) / baud;  // baud from the live clock
    u->regs->CR1 |= (USART_CR1_TE | USART_CR1_RE);
    u->regs->CR1 |= USART_CR1_UE;
    u->regs->CR1 |= USART_CR1_RXNEIE;  // rx interrupt on; tx armed on demand in uart_write
    NVIC_EnableIRQ(irqn);
}

void uart_console_init(uint32_t baud) {
    // usart2 on apb1, pins pa2/pa3 = af7
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB1ENR;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;

    GPIOA->MODER &= ~(GPIO_MODER_MODE2_Msk | GPIO_MODER_MODE3_Msk);
    GPIOA->MODER |= (GPIO_MODER_MODE2_1 | GPIO_MODER_MODE3_1);  // alternate function
    GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL2_Msk | GPIO_AFRL_AFSEL3_Msk);
    GPIOA->AFR[0] |= (0x7UL << GPIO_AFRL_AFSEL2_Pos) | (0x7UL << GPIO_AFRL_AFSEL3_Pos);

    uart_start(uart_console, clock_pclk1_hz(), USART2_IRQn, baud);
}

void uart_downlink_init(uint32_t baud) {
    // usart6 on apb2, pins pc6/pc7 = af8
    RCC->APB2ENR |= RCC_APB2ENR_USART6EN;
    (void)RCC->APB2ENR;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    (void)RCC->AHB1ENR;

    GPIOC->MODER &= ~(GPIO_MODER_MODE6_Msk | GPIO_MODER_MODE7_Msk);
    GPIOC->MODER |= (GPIO_MODER_MODE6_1 | GPIO_MODER_MODE7_1);  // alternate function
    GPIOC->AFR[0] &= ~(GPIO_AFRL_AFSEL6_Msk | GPIO_AFRL_AFSEL7_Msk);
    GPIOC->AFR[0] |= (0x8UL << GPIO_AFRL_AFSEL6_Pos) | (0x8UL << GPIO_AFRL_AFSEL7_Pos);

    uart_start(uart_downlink, clock_pclk2_hz(), USART6_IRQn, baud);
}

void uart_write(uart_t* u, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint16_t next = (uint16_t)((u->tx_head + 1U) & (BUF_SIZE - 1U));
        while (next == u->tx_tail) {
            __WFI();  // tx buffer full - sleep until an interrupt (the txe isr) makes room
        }
        u->tx_buf[u->tx_head] = data[i];
        u->tx_head = next;
        u->regs->CR1 |= USART_CR1_TXEIE;  // kick the tx interrupt
    }
}

bool uart_tx_done(uart_t* u) { return (u->tx_head == u->tx_tail) && (u->regs->SR & USART_SR_TC); }

void uart_flush(uart_t* u) {
    while (!uart_tx_done(u)) {
    }
}

bool uart_read_byte(uart_t* u, uint8_t* out) {
    if (u->rx_head == u->rx_tail) {
        return false;
    }
    *out = u->rx_buf[u->rx_tail];
    u->rx_tail = (uint16_t)((u->rx_tail + 1U) & (BUF_SIZE - 1U));
    return true;
}

size_t uart_rx_available(uart_t* u) {
    return (size_t)((u->rx_head - u->rx_tail) & (BUF_SIZE - 1U));
}

void uart_get_errors(uart_t* u, uart_errors_t* out) {
    out->overrun = u->errors.overrun;
    out->framing = u->errors.framing;
    out->noise = u->errors.noise;
    out->dropped = u->errors.dropped;
}

// shared isr body - same logic for any instance, dispatched by the vectors below
static void uart_isr(uart_t* u) {
    uint32_t sr = u->regs->SR;

    // receive-line errors - cleared together with rxne by the dr read below
    if (sr & USART_SR_ORE) {
        u->errors.overrun++;
    }
    if (sr & USART_SR_FE) {
        u->errors.framing++;
    }
    if (sr & USART_SR_NE) {
        u->errors.noise++;
    }

    // byte arrived (reading dr clears rxne, and any pending ore/fe/ne)
    if (sr & USART_SR_RXNE) {
        uint8_t b = (uint8_t)u->regs->DR;
        uint16_t next = (uint16_t)((u->rx_head + 1U) & (BUF_SIZE - 1U));
        if (next != u->rx_tail) {
            u->rx_buf[u->rx_head] = b;
            u->rx_head = next;
        } else {
            u->errors.dropped++;  // rx buffer full - byte discarded
        }
    }

    // ready for next tx byte
    if ((sr & USART_SR_TXE) && (u->regs->CR1 & USART_CR1_TXEIE)) {
        if (u->tx_head != u->tx_tail) {
            u->regs->DR = u->tx_buf[u->tx_tail];
            u->tx_tail = (uint16_t)((u->tx_tail + 1U) & (BUF_SIZE - 1U));
        }
        if (u->tx_head == u->tx_tail) {
            u->regs->CR1 &= ~USART_CR1_TXEIE;  // buffer empty - disable or it storms
        }
    }
}

// override the weak vectors in startup_stm32f446retx.s
void USART2_IRQHandler(void) { uart_isr(uart_console); }
void USART6_IRQHandler(void) { uart_isr(uart_downlink); }
