/**
 * @file   health_task.cpp
 * @brief  collects per-task liveness and stack margin, and reports it (REQ-RT-003)
 *
 * This task becomes the watchdog: the liveness it already gathers is exactly what the IWDG service
 * has to judge before it agrees to pet the dog, so the report lands first and the enforcement is a
 * small addition on top of it.
 */

#include "drivers/iwdg.h"
#include "drivers/systick.h"
#include "drivers/uart.h"
#include "protocol/frame.hpp"
#include "protocol/msg.hpp"
#include "rtos_tasks.h"
#include "task_health.hpp"

namespace {

constexpr uint32_t kReportPeriodMs = 1000U;  // 1 hz, same cadence as the heartbeat

// one slot per TaskId. a task writes only its own last_ms and the reader only reads, so a single
// aligned 32-bit store is the whole transaction - atomic on armv7-m, no mutex earned. handles are
// written before the scheduler starts, so they are settled by the time anything reads them
struct slot_t {
    TaskHandle_t handle;
    uint32_t deadline_ms;  // 0 = reported but not judged
    volatile uint32_t last_ms;
};

slot_t s_slots[TASK_ID_COUNT];

StaticTask_t s_tcb;
StackType_t s_stack[TASK_STACK_HEALTH];

// ms since a task's last check-in, in the wire's saturating form. last_ms of 0 means it has never
// checked in: board bring-up alone takes over 100 ms, so no real check-in can land on 0
uint16_t checkin_age(const slot_t& s, uint32_t now_ms) {
    if (s.last_ms == 0U) {
        return 0xFFFFU;
    }
    const uint32_t age = now_ms - s.last_ms;
    return (age >= 0xFFFFU) ? 0xFFFEU : static_cast<uint16_t>(age);
}

void health_task(void*) {
    TickType_t next = xTaskGetTickCount();

    // the idle task does not exist until the scheduler creates it, so it cannot be registered from
    // a create function like the others. no deadline: it never checks in, and "idle did not run"
    // is not a fault - it means everything else had work to do
    task_health_register(TASK_ID_IDLE, xTaskGetIdleTaskHandle(), 0U);

    for (;;) {
        // wait first, report second. this task is the highest priority of the periodic set, so on
        // the very first pass it would otherwise run before any lower-priority task had checked in
        // even once, judge them all stale, and cry watchdog on a perfectly healthy boot
        xTaskDelayUntil(&next, pdMS_TO_TICKS(kReportPeriodMs));

        // before the snapshot, not after: this task's own age is the one number in the report that
        // would otherwise always read as a full period stale. its liveness is implicit anyway -
        // the report only exists because it ran, and the iwdg will only be petted from here
        task_health_checkin(TASK_ID_HEALTH);

        const uint32_t now = millis();

        fsw::task_health_t h{};
        h.t_ms = now;

        bool all_alive = true;

        for (uint8_t id = 0U; id < TASK_ID_COUNT; id++) {
            const slot_t& s = s_slots[id];
            if (s.handle == nullptr) {
                continue;  // slot reserved for a task that does not exist yet
            }

            const UBaseType_t free_words = uxTaskGetStackHighWaterMark(s.handle);
            const uint16_t age = checkin_age(s, now);

            // a task past its deadline is what withholds the pet. a deadline of 0 opts out, which
            // is how the idle task and this one stay unjudged - judging the reporter would be
            // circular, since a health task that stops running stops petting either way
            if (s.deadline_ms != 0U && age > s.deadline_ms) {
                all_alive = false;
            }

            fsw::task_entry_t& e = h.tasks[h.count];
            e.id = id;
            e.state = static_cast<uint8_t>(eTaskGetState(s.handle));
            e.stack_free_words =
                (free_words > 0xFFFFU) ? 0xFFFFU : static_cast<uint16_t>(free_words);
            e.checkin_age_ms = age;
            h.count++;
        }

        // the whole point of the phase: the watchdog is serviced only on evidence that every task
        // judged for liveness is still running. stop petting and the board resets itself, and the
        // next boot banner says the watchdog did it (REQ-WDG-001, REQ-WDG-002)
        if (all_alive) {
            iwdg_pet();
            h.flags = fsw::kTaskHealthFlagWatchdogFed;
        }

        uint8_t buf[fsw::kFrameMaxSize];
        const size_t n = fsw::frame_encode(static_cast<uint8_t>(fsw::MsgId::TaskHealth),
                                           reinterpret_cast<const uint8_t*>(&h), sizeof(h), buf);
        // both links, like every other piece of housekeeping - a contact pass wants to know the
        // computer is healthy as much as the bench does
        uart_write(uart_console, buf, n);
        uart_write(uart_downlink, buf, n);
    }
}

}  // namespace

void task_health_register(TaskId id, TaskHandle_t task, uint32_t deadline_ms) {
    if (id < TASK_ID_COUNT) {
        s_slots[id].handle = task;
        s_slots[id].deadline_ms = deadline_ms;
    }
}

void task_health_checkin(TaskId id) {
    if (id < TASK_ID_COUNT) {
        s_slots[id].last_ms = millis();
    }
}

void health_task_create(void) {
    const TaskHandle_t h = xTaskCreateStatic(health_task, "health", TASK_STACK_HEALTH, nullptr,
                                             TASK_PRIO_HEALTH, s_stack, &s_tcb);
    task_health_register(TASK_ID_HEALTH, h, 0U);  // the reporter does not judge itself
}
