/**
 * @file   uart.h
 * @brief  uart driver - usart2 serial i/o (interrupt-driven tx + rx)
 */

#ifndef UART_H
#define UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief  bring up usart2 (pa2/pa3, af7) at the given baud, 8n1
 * @param  baud  baud rate in bits per second
 */
void uart_init(uint32_t baud);

/**
 * @brief  transmit a block of bytes
 * @param  data  pointer to the bytes to send
 * @param  len   number of bytes
 */
void uart_write(const uint8_t* data, size_t len);

/**
 * @brief  whether all queued tx bytes have fully left the wire
 * @return true when the tx buffer is empty and the last byte has shifted out
 */
bool uart_tx_done(void);

/**
 * @brief  block until all queued tx bytes have been sent
 */
void uart_flush(void);

/**
 * @brief  take one received byte from the rx buffer, if any (non-blocking)
 * @param  out  destination for the byte when one is available
 * @return true if a byte was written to @p out, false if the buffer was empty
 */
bool uart_read_byte(uint8_t* out);

/**
 * @brief  number of received bytes waiting in the rx buffer
 * @return count of unread bytes
 */
size_t uart_rx_available(void);

// receive-line error counts since boot
typedef struct {
    uint32_t overrun;  // hardware lost a byte before the isr read it (ore)
    uint32_t framing;  // a stop bit was missing (baud mismatch, glitch, break)
    uint32_t noise;    // oversampling detected noise on a bit
    uint32_t dropped;  // the isr read a byte but the rx buffer was full
} uart_errors_t;

/**
 * @brief  copy the current receive-line error counts
 * @param  out  destination for the counts
 */
void uart_get_errors(uart_errors_t* out);

#endif  // UART_H
