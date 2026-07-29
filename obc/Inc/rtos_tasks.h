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

#define TASK_PRIO_CONTROL (configMAX_PRIORITIES - 1)
#define TASK_PRIO_HEALTH  6
#define TASK_PRIO_SENSORS 5

// sized against measured peaks, not guesses. bench run 2026-08-02 covering every mode, a capture
// and a 131-chunk downlink reported the same high-water marks as an idle run - the deepest path in
// the control task is a console_printf, not the payload downlink. peaks were control 181, sensors
// 101, health 80, idle 23 words; each below keeps roughly 2.5x that. the margin is deliberately
// generous rather than tight, because the kernel's high-water figure reads optimistic: it counts a
// fill pattern, and a written byte that happens to hold that pattern lets the count run past the
// true frontier. re-measure and re-size when a task's work changes
#define TASK_STACK_CONTROL 512U
#define TASK_STACK_SENSORS 256U
#define TASK_STACK_HEALTH  192U  // one small struct and a frame buffer, no formatted output

#endif  // RTOS_TASKS_H
