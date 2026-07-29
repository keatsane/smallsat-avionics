/**
 * @file   uart.c
 * @brief  uart driver - interrupt-driven tx + rx, shared across usart instances
 */

#include "drivers/uart.h"

#include "FreeRTOS.h"
#include "board.h"
#include "drivers/clock.h"
#include "drivers/gpio.h"
#include "semphr.h"
#include "stm32f446xx.h"
#include "task.h"

#define BUF_SIZE 256U  // power of 2 - the wrap masks below depend on it

#define UART_BAUD 115200U  // all three links run the same rate

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

    // tx is one shared resource with more than one writer task, so it gets a mutex - that is what
    // mutexes are for here, never for shared flight state. rx needs none: the isr is the only
    // producer and one task the only consumer
    SemaphoreHandle_t tx_mutex;
    StaticSemaphore_t tx_mutex_buf;
};

// uart_init fills regs into the handle from the cfg below, so the instances start empty
static struct uart uart_esc_inst;
uart_t* const uart_esc = &uart_esc_inst;

static struct uart uart_console_inst;
uart_t* const uart_console = &uart_console_inst;

static struct uart uart_downlink_inst;
uart_t* const uart_downlink = &uart_downlink_inst;

// which apb - picks the enable bit and the baud clock together, so they cannot disagree
typedef enum {
    UART_BUS_APB1 = 0,
    UART_BUS_APB2 = 1,
} uart_bus_t;

// all that differs between links: the peripheral, its bus + clock-enable bit, the pin map, the irq
typedef struct {
    USART_TypeDef* regs;
    uart_bus_t bus;
    uint32_t enr_bit;
    GPIO_TypeDef* port;  // tx and rx share one port on all three
    uint32_t tx_pin;
    uint32_t rx_pin;
    uint8_t af;
    IRQn_Type irq;
    uint32_t baud;
} uart_cfg_t;

// bring up one link from its cfg: clock on, both af pins, baud, enable tx/rx, arm rx interrupt
static void uart_init(uart_t* u, const uart_cfg_t* c) {
    u->regs = c->regs;

    uint32_t pclk_hz;
    if (c->bus == UART_BUS_APB2) {
        RCC->APB2ENR |= c->enr_bit;
        (void)RCC->APB2ENR;
        pclk_hz = clock_pclk2_hz();
    } else {
        RCC->APB1ENR |= c->enr_bit;
        (void)RCC->APB1ENR;
        pclk_hz = clock_pclk1_hz();
    }

    gpio_enable_port(c->port);
    gpio_config_af(c->port, c->tx_pin, c->af, GPIO_PUSH_PULL, GPIO_SPEED_LOW);
    gpio_config_af(c->port, c->rx_pin, c->af, GPIO_PUSH_PULL, GPIO_SPEED_LOW);

    u->regs->BRR = (pclk_hz + c->baud / 2U) / c->baud;  // baud from the live clock
    u->regs->CR1 |= (USART_CR1_TE | USART_CR1_RE);
    u->regs->CR1 |= USART_CR1_UE;
    u->regs->CR1 |= USART_CR1_RXNEIE;         // rx interrupt on; tx armed on demand in uart_write
    NVIC_SetPriority(c->irq, IRQ_PRIO_UART);  // before enabling - the default is 0, see board.h
    NVIC_EnableIRQ(c->irq);
}

void uart_esc_init(void) {
    static const uart_cfg_t cfg = {
        .regs = ESC_UART,
        .bus = UART_BUS_APB2,
        .enr_bit = RCC_APB2ENR_USART1EN,
        .port = ESC_PORT,
        .tx_pin = ESC_TX_PIN,
        .rx_pin = ESC_RX_PIN,
        .af = ESC_AF,
        .irq = ESC_IRQ,
        .baud = UART_BAUD,
    };
    uart_init(uart_esc, &cfg);
}

void uart_console_init(void) {
    static const uart_cfg_t cfg = {
        .regs = CONSOLE_UART,
        .bus = UART_BUS_APB1,
        .enr_bit = RCC_APB1ENR_USART2EN,
        .port = CONSOLE_PORT,
        .tx_pin = CONSOLE_TX_PIN,
        .rx_pin = CONSOLE_RX_PIN,
        .af = CONSOLE_AF,
        .irq = CONSOLE_IRQ,
        .baud = UART_BAUD,
    };
    uart_init(uart_console, &cfg);
}

void uart_downlink_init(void) {
    static const uart_cfg_t cfg = {
        .regs = DOWNLINK_UART,
        .bus = UART_BUS_APB2,
        .enr_bit = RCC_APB2ENR_USART6EN,
        .port = DOWNLINK_PORT,
        .tx_pin = DOWNLINK_TX_PIN,
        .rx_pin = DOWNLINK_RX_PIN,
        .af = DOWNLINK_AF,
        .irq = DOWNLINK_IRQ,
        .baud = UART_BAUD,
    };
    uart_init(uart_downlink, &cfg);
}

// hold the tx line for the whole of one write, so two tasks cannot interleave their bytes into one
// unreadable frame. board bring-up writes before the scheduler exists, so the lock is conditional -
// and there is no contention then either, since there is only one thread of execution.
//
// the mutex is held across the wait-for-room spin below, which is deliberate: a frame half in the
// ring is not a frame, so the writer keeps the line until its last byte is queued. freertos mutexes
// inherit priority, so a lower-priority writer holding it gets boosted to the waiter's priority and
// the block is bounded by how fast the ring drains (256 bytes, ~22 ms at 115200)
static bool tx_lock(uart_t* u) {
    if (u->tx_mutex == NULL || xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return false;
    }
    return xSemaphoreTake(u->tx_mutex, portMAX_DELAY) == pdTRUE;
}

void uart_locks_init(void) {
    uart_esc->tx_mutex = xSemaphoreCreateMutexStatic(&uart_esc->tx_mutex_buf);
    uart_console->tx_mutex = xSemaphoreCreateMutexStatic(&uart_console->tx_mutex_buf);
    uart_downlink->tx_mutex = xSemaphoreCreateMutexStatic(&uart_downlink->tx_mutex_buf);
}

void uart_write(uart_t* u, const uint8_t* data, size_t len) {
    const bool locked = tx_lock(u);

    for (size_t i = 0; i < len; i++) {
        uint16_t next = (uint16_t)((u->tx_head + 1U) & (BUF_SIZE - 1U));
        while (next == u->tx_tail) {
            __WFI();  // tx buffer full - sleep until an interrupt (the txe isr) makes room
        }
        u->tx_buf[u->tx_head] = data[i];
        u->tx_head = next;
        u->regs->CR1 |= USART_CR1_TXEIE;  // kick the tx interrupt
    }

    if (locked) {
        (void)xSemaphoreGive(u->tx_mutex);
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

uart_errors_t uart_get_errors(uart_t* u) {
    uart_errors_t e;
    e.overrun = u->errors.overrun;
    e.framing = u->errors.framing;
    e.noise = u->errors.noise;
    e.dropped = u->errors.dropped;
    return e;
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
void USART1_IRQHandler(void) { uart_isr(uart_esc); }
void USART2_IRQHandler(void) { uart_isr(uart_console); }
void USART6_IRQHandler(void) { uart_isr(uart_downlink); }
