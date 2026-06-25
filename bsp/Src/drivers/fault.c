/**
 * @file   fault.c
 * @brief  hard fault handler - capture the crash context, report it, then reboot
 */

#include "board.h"
#include "stm32f446xx.h"

void hardfault_report(const uint32_t* frame);  // called from the naked handler below

// polled, self-contained console write for the fault path. the normal uart driver
// is interrupt-driven, and its tx isr can't run inside the higher-priority fault
// handler - so we bypass the driver and push bytes straight to the usart, polling
// the txe flag ourselves
static void panic_putc(char c) {
    while ((CONSOLE_UART->SR & USART_SR_TXE) == 0U) {
    }
    CONSOLE_UART->DR = (uint8_t)c;
}

static void panic_puts(const char* s) {
    while (*s != '\0') {
        panic_putc(*s++);
    }
}

static void panic_hex(uint32_t v) {
    panic_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t nib = (v >> shift) & 0xFU;
        panic_putc((char)(nib < 10U ? ('0' + nib) : ('a' + nib - 10U)));
    }
}

// the c half of the handler - `frame` points at the registers the cortex-m
// auto-stacked on fault entry: r0 r1 r2 r3 r12 lr pc xpsr, in that order
void hardfault_report(const uint32_t* frame) {
    uint32_t lr = frame[5];
    uint32_t pc = frame[6];  // the instruction that faulted

    CONSOLE_UART->CR1 &= ~USART_CR1_TXEIE;  // take the tx line off the stalled isr path

    panic_puts("\r\nHARDFAULT pc=");
    panic_hex(pc);
    panic_puts(" lr=");
    panic_hex(lr);
    panic_puts(" cfsr=");
    panic_hex(SCB->CFSR);  // which fault fired (mem/bus/usage bits)
    panic_puts(" hfsr=");
    panic_hex(SCB->HFSR);
    panic_puts("\r\n");

    while ((CONSOLE_UART->SR & USART_SR_TC) == 0U) {  // let the last byte clear the wire
    }

    NVIC_SystemReset();  // controlled reboot - next boot reads reset=software
    for (;;) {
    }
}

// overrides the weak HardFault_Handler in the startup file. naked = no compiler
// prologue, so the faulting stack pointer stays untouched: exc_return bit 2 (in lr)
// picks which stack was in use, and we hand that pointer to hardfault_report in r0
__attribute__((naked)) void HardFault_Handler(void) {
    __asm volatile(
        "tst lr, #4         \n"
        "ite eq             \n"
        "mrseq r0, msp      \n"
        "mrsne r0, psp      \n"
        "b hardfault_report \n");
}
