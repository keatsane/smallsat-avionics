/**
 * @file   rtos_tasks.h
 * @brief  the task table - every priority and stack size in one place
 *
 * Priorities are only meaningful relative to each other, so they belong in one file rather than
 * being chosen a task at a time. Highest number wins; time slicing is off, so no two tasks share
 * a priority (configUSE_TIME_SLICING 0 - two at the same level would only run in an order nothing
 * guarantees).
 *
 * The order, most urgent first, is fixed by what starving each one costs:
 *   control    the fixed-rate cycle - a late cycle is a late control response (REQ-RT-003)
 *   watchdog   must be able to check liveness even while the buses are busy
 *   sensors    blocks on i2c/spi, so it cannot sit inside the control loop
 *   uplink     blocks on uart rx instead of polling
 *   telemetry  owns all console output; a slow console must never delay a decision
 *   downlink   payload chunks, the one job that should always yield first
 *
 * Only the tasks that exist are declared below; each lands with its own entry.
 *
 * Stacks are in words, not bytes, and start deliberately generous. REQ-RT-003 reports the
 * high-water mark of each, and that measurement is what these get trimmed against - guessing low
 * and finding out on the bench is the failure this ordering avoids.
 */

#ifndef RTOS_TASKS_H
#define RTOS_TASKS_H

#include "FreeRTOS.h"
#include "task.h"

#define TASK_PRIO_CONTROL   (configMAX_PRIORITIES - 1)
#define TASK_PRIO_HEALTH    6
#define TASK_PRIO_SENSORS   5
#define TASK_PRIO_UPLINK    4
#define TASK_PRIO_TELEMETRY 3
#define TASK_PRIO_DOWNLINK  2

// sized against measured peaks, not guesses. bench run 2026-07-29 covering every mode, a capture
// and a 131-chunk downlink reported the same high-water marks as an idle run - the deepest path in
// the control task is a console_printf, not the payload downlink. peaks were control 181, sensors
// 101, health 80, idle 23 words; each below keeps roughly 2.5x that. the margin is deliberately
// generous rather than tight, because the kernel's high-water figure reads optimistic: it counts a
// fill pattern, and a written byte that happens to hold that pattern lets the count run past the
// true frontier. re-measure and re-size when a task's work changes
#define TASK_STACK_CONTROL 512U
#define TASK_STACK_SENSORS 256U
#define TASK_STACK_HEALTH  192U  // one small struct and a frame buffer, no formatted output
// measured 2026-07-30: idle it peaks near 40 words, but decoding one command took it to 113 -
// frame_decode returns std::optional<frame_t> by value and that carries the 64-byte payload
// buffer, which at -O0 lands on the stack whole. sized at ~2.8x the measured peak, matching
// control's ratio rather than the 192 this started at, which left it the tightest task in the
// system on the one figure known to read optimistic
#define TASK_STACK_UPLINK 320U
// its drain scratch is static rather than on the stack, so what is left is a uart_write frame and
// the 96-byte line buffer of the one console_printf it makes. measured 2026-07-30: 59 words in
// steady state, leaving 197 free. kept at 256 rather than trimmed to the house ~2.8x ratio because
// that run never dropped a message, so the console_printf path - the deepest one here, ~24 words
// of line buffer - was never taken. re-measure once a TLM DROPPED line has actually been printed
#define TASK_STACK_TELEMETRY 256U
// a payload_data_t and an encoded frame are both on the stack at once, ~130 bytes of the two
// buffers before anything else. generous pending a measured peak
#define TASK_STACK_DOWNLINK 256U

#endif  // RTOS_TASKS_H
