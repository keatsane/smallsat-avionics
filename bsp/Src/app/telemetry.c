/**
 * @file   telemetry.c
 * @brief  build and send periodic telemetry messages over the link
 */

#include "app/telemetry.h"

#include "drivers/systick.h"
#include "drivers/uart.h"
#include "protocol/frame.h"
#include "protocol/msg.h"
#include "protocol/state.h"

static uint16_t hb_seq;  // heartbeat sequence counter

void telemetry_send_heartbeat(void) {
    heartbeat_t hb = {
        .uptime_ms = millis(),
        .mode = MODE_BOOT,
        .faults = 0,
        .seq = hb_seq++,
    };
    uint8_t frame[FRAME_MAX_SIZE];
    size_t n = frame_encode(MSG_HEARTBEAT, (const uint8_t*)&hb, sizeof(hb), frame);
    uart_write(uart_console, frame, n);   // usb monitor
    uart_write(uart_downlink, frame, n);  // header pin - scope now, radio later
}

void telemetry_send_link_status(void) {
    uart_errors_t errs;
    uart_get_errors(uart_console, &errs);  // host-link receive health
    link_status_t st = {
        .overrun = errs.overrun,
        .framing = errs.framing,
        .noise = errs.noise,
        .dropped = errs.dropped,
    };
    uint8_t frame[FRAME_MAX_SIZE];
    size_t n = frame_encode(MSG_LINK_STATUS, (const uint8_t*)&st, sizeof(st), frame);
    uart_write(uart_console, frame, n);
    uart_write(uart_downlink, frame, n);
}
