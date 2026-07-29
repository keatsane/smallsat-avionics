/**
 * @file   task_health.hpp
 * @brief  per-task liveness check-ins and the report that carries them (REQ-RT-003)
 *
 * Each task records that it finished a pass; the health task reads every record, adds each task's
 * stack margin, and sends the lot down as one snapshot. Nothing here reaches the flight software -
 * the fsw stays single-threaded and unaware that tasks exist.
 *
 * The ids are a wire contract: the ground maps an id to a name, so a slot keeps its number even
 * before the task that owns it exists.
 */

#ifndef TASK_HEALTH_HPP
#define TASK_HEALTH_HPP

#include "FreeRTOS.h"
#include "task.h"

enum TaskId : uint8_t {
    TASK_ID_CONTROL = 0,
    TASK_ID_SENSORS = 1,
    TASK_ID_HEALTH = 2,     // this task - it becomes the watchdog when the iwdg lands
    TASK_ID_UPLINK = 3,     // reserved
    TASK_ID_TELEMETRY = 4,  // reserved
    TASK_ID_DOWNLINK = 5,   // reserved
    TASK_ID_IDLE = 6,       // the kernel's own - reported for its stack, never checks in
    TASK_ID_COUNT = 7,
};

/**
 * @brief  claim a slot in the report for a task
 * call from the task's create function, before the scheduler starts
 * @param  id    which slot
 * @param  task  the handle, for stack and state queries
 */
void task_health_register(TaskId id, TaskHandle_t task);

/**
 * @brief  record that this task just finished a pass
 * call at the end of the loop body rather than the start - a task that wakes and then hangs
 * mid-pass has not done its job, and only an end-of-pass check-in catches that
 * @param  id  the caller's slot
 */
void task_health_checkin(TaskId id);

/**
 * @brief  create the health task
 * call after the other tasks are created and before the scheduler starts
 */
void health_task_create(void);

#endif  // TASK_HEALTH_HPP
