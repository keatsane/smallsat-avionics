/**
 * @file   uart.h
 * @brief  uart driver - interrupt-driven tx + rx, one handle per usart instance
 */

#ifndef UART_H
#define UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// opaque per-instance handle; the three board uarts are exposed below
typedef struct uart uart_t;

// usart1 -> pa9/pa10: the esc command link (reaction wheel)
extern uart_t* const uart_esc;
// usart2 -> st-link vcp over usb: debug console + dev-time telemetry monitor (ground only)
extern uart_t* const uart_console;
// usart6 -> pc6/pc7 header: the telemetry downlink (scope/logic-analyzer now, lora later)
extern uart_t* const uart_downlink;

// receive-line error counts since boot
typedef struct {
    uint32_t overrun;  // hardware lost a byte before the isr read it (ore)
    uint32_t framing;  // a stop bit was missing (baud mismatch, glitch, break)
    uint32_t noise;    // oversampling detected noise on a bit
    uint32_t dropped;  // the isr read a byte but the rx buffer was full
} uart_errors_t;

/**
 * @brief  bring up the esc uart (usart1, pa9/pa10, af7) at 115200 8n1
 */
void uart_esc_init(void);

/**
 * @brief  bring up the console uart (usart2, pa2/pa3, af7) at 115200 8n1
 */
void uart_console_init(void);

/**
 * @brief  bring up the downlink uart (usart6, pc6/pc7, af8) at 115200 8n1
 */
void uart_downlink_init(void);

/**
 * @brief  create the per-uart transmit mutexes
 * separate from the init functions on purpose: board bring-up must not call into the kernel, since
 * a failure there happens before the console can report it. call once after the board is up and
 * before the scheduler starts - until then uart_write serialises nothing, which is correct while
 * there is only one thread of execution
 */
void uart_locks_init(void);

/**
 * @brief  transmit a block of bytes
 * holds the uart's transmit mutex for the whole write once the scheduler is running, so two tasks
 * cannot interleave bytes into one frame
 * @param  u     target uart handle
 * @param  data  pointer to the bytes to send
 * @param  len   number of bytes
 */
void uart_write(uart_t* u, const uint8_t* data, size_t len);

/**
 * @brief  whether all queued tx bytes have fully left the wire
 * @param  u  target uart handle
 * @return true when the tx buffer is empty and the last byte has shifted out
 */
bool uart_tx_done(uart_t* u);

/**
 * @brief  block until all queued tx bytes have been sent
 * @param  u  target uart handle
 */
void uart_flush(uart_t* u);

/**
 * @brief  take one received byte from the rx buffer, if any (non-blocking)
 * @param  u    target uart handle
 * @param  out  destination for the byte when one is available
 * @return true if a byte was written to @p out, false if the buffer was empty
 */
bool uart_read_byte(uart_t* u, uint8_t* out);

/**
 * @brief  have this uart's rx isr notify a task when a byte lands
 * @param  u    the uart
 * @param  task the task to notify, or NULL to stop notifying
 *
 * Lets a reader block on arrival instead of polling the ring every control cycle. The isr gives a
 * counting notification and nothing else - the task still drains with uart_read_byte, so the
 * one-producer/one-consumer rule on the rx ring is unchanged. Two uarts may notify the same task;
 * the notification says "something arrived", not which port.
 *
 * Must be called before the scheduler starts, or from the task that will wait.
 */
void uart_set_rx_waiter(uart_t* u, void* task);

/**
 * @brief  number of received bytes waiting in the rx buffer
 * @param  u  target uart handle
 * @return count of unread bytes
 */
size_t uart_rx_available(uart_t* u);

/**
 * @brief  snapshot the current receive-line error counts
 * @param  u  target uart handle
 * @return the error counts
 */
uart_errors_t uart_get_errors(uart_t* u);

#ifdef __cplusplus
}
#endif

#endif  // UART_H
