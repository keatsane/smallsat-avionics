/**
 * @file   uplink_task.cpp
 * @brief  ground commands off both uplinks, decoded and queued for the control task
 *
 * The control task used to drain both uarts itself every cycle. Two things were wrong with that:
 * it polled a link that is silent almost always, and fsw::Inputs carries one command, so a burst
 * arriving inside one 100 ms cycle kept only the last and silently dropped the rest.
 *
 * Here the task blocks until the rx isr says a byte landed, decodes whole frames, and queues each
 * command. The control task takes one per cycle; the rest wait rather than vanish.
 *
 * Both uplinks are drained on every wake-up. The ground can reach the obc over the st-link vcp on
 * the bench or the pc6/pc7 header the radio lands on, telemetry already goes out both, and
 * draining unconditionally rather than stopping at the first hit keeps the quiet one's ring from
 * filling up.
 */

#include "uplink_task.hpp"

#include <cstring>

#include "FreeRTOS.h"
#include "devices/rfm95.h"
#include "drivers/uart.h"
#include "protocol/frame.hpp"
#include "queue.h"
#include "rtos_tasks.h"
#include "task.h"
#include "task_health.hpp"

namespace {

// deep enough to hold a burst the control task will take several cycles to work through, shallow
// enough that a flood is dropped at the door rather than delivering stale commands minutes later.
// a full queue is the honest outcome: the ground gets no ack and resends (REQ-CMD-003)
constexpr UBaseType_t kQueueDepth = 8;

StaticTask_t s_tcb;
StackType_t s_stack[TASK_STACK_UPLINK];

QueueHandle_t s_queue;
StaticQueue_t s_queue_buf;
uint8_t s_queue_storage[kQueueDepth * sizeof(fsw::command_t)];

// decode whatever is waiting on one uart, queueing every whole command frame found
void drain(uart_t* u, fsw::frame_parser_t* parser) {
    uint8_t b = 0U;

    while (uart_read_byte(u, &b)) {
        auto f = fsw::frame_decode(parser, b);
        if (f && f->msg_id == static_cast<uint8_t>(fsw::MsgId::Command) &&
            f->len == sizeof(fsw::command_t)) {
            fsw::command_t cmd{};
            std::memcpy(&cmd, f->payload, sizeof(cmd));
            // drop rather than block: this task must keep draining the rings, and blocking here
            // to wait for room would stall both uplinks behind one slow consumer
            (void)xQueueSend(s_queue, &cmd, 0);
        }
    }
}

// the lora radio, drained the same way and into the same queue. a command that arrived over the
// air is not a different kind of command, so nothing downstream of here can tell which link it
// came in on - which is the whole reason the wire format is transport-independent
void drain_radio(void) {
    static fsw::frame_parser_t lora_parser;
    uint8_t packet[fsw::kFrameMaxSize];

    const size_t n = rfm95_receive(packet, sizeof(packet));
    for (size_t i = 0U; i < n; i++) {
        auto f = fsw::frame_decode(&lora_parser, packet[i]);
        if (f && f->msg_id == static_cast<uint8_t>(fsw::MsgId::Command) &&
            f->len == sizeof(fsw::command_t)) {
            fsw::command_t cmd{};
            std::memcpy(&cmd, f->payload, sizeof(cmd));
            (void)xQueueSend(s_queue, &cmd, 0);
        }
    }
}

void uplink_task(void*) {
    static fsw::frame_parser_t console_parser;
    static fsw::frame_parser_t radio_parser;

    // the isr notifies this task by handle, so it has to be registered from inside the task
    uart_set_rx_waiter(uart_console, xTaskGetCurrentTaskHandle());
    uart_set_rx_waiter(uart_downlink, xTaskGetCurrentTaskHandle());

    for (;;) {
        // wake on arrival, but time out anyway so liveness is reported on a silent link - a task
        // that only checks in when the ground talks would look hung to the watchdog on any bench
        // with no ground station attached
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200U));

        drain(uart_console, &console_parser);
        drain(uart_downlink, &radio_parser);
        drain_radio();

        task_health_checkin(TASK_ID_UPLINK);
    }
}

}  // namespace

bool uplink_task_take(fsw::command_t* out) {
    return s_queue != nullptr && xQueueReceive(s_queue, out, 0) == pdTRUE;
}

void uplink_task_create(void) {
    s_queue =
        xQueueCreateStatic(kQueueDepth, sizeof(fsw::command_t), s_queue_storage, &s_queue_buf);

    const TaskHandle_t h = xTaskCreateStatic(uplink_task, "uplink", TASK_STACK_UPLINK, nullptr,
                                             TASK_PRIO_UPLINK, s_stack, &s_tcb);
    // generous against the 200 ms wake-up: a silent link still checks in five times a second, so
    // anything past a second means the task is genuinely stuck rather than merely idle
    task_health_register(TASK_ID_UPLINK, h, 1000U);
}
