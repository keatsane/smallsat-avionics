/**
 * @file   telemetry_task.cpp
 * @brief  drains the output buffers to the console and downlink uarts
 */

#include "telemetry_task.hpp"

#include "FreeRTOS.h"
#include "console.hpp"
#include "devices/rfm95.h"
#include "drivers/uart.h"
#include "protocol/frame.hpp"
#include "rtos_tasks.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "task.h"
#include "task_health.hpp"

namespace {

// one busy cycle offers roughly 125 bytes of telemetry per link, and about 390 in DOWNLINK once
// the payload chunks join it. the wire carries ~1150 bytes in the same 100 ms, so the link is not
// saturated - it is bursty, and this is the shock absorber for the burst rather than a queue that
// is meant to run deep. 1 KB holds two and a half worst cycles
constexpr size_t kBufBytes = 1024;

// wake the task as soon as a single byte lands rather than waiting for a fill level - latency to
// the wire is the thing being bought here
constexpr size_t kTriggerBytes = 1;

// how long to sit waiting for console traffic before going round anyway. bounded so the task keeps
// checking in on a build that prints nothing, and so the downlink is still serviced when the
// console happens to be idle
constexpr TickType_t kIdleWaitMs = 50;

StaticTask_t s_tcb;
StackType_t s_stack[TASK_STACK_TELEMETRY];

StreamBufferHandle_t s_console;
StaticStreamBuffer_t s_console_sb;
uint8_t s_console_store[kBufBytes];

StreamBufferHandle_t s_downlink;
StaticStreamBuffer_t s_downlink_sb;
uint8_t s_downlink_store[kBufBytes];

// producers are the control, health and payload tasks, and stream buffers are only safe with one
// writer - so the write side is serialised. the lock covers a space check and a memcpy, never a
// wait on hardware, which is what keeps this cheap enough to take from the control task
SemaphoreHandle_t s_lock;
StaticSemaphore_t s_lock_buf;

uint32_t s_dropped = 0;  // guarded by s_lock

// only this task reads, so the drain scratch can be static and keep the stack small
uint8_t s_drain[128];

// the lora beacon's outbound queue, guarded by s_lock.
//
// this was one slot deep, on the reasoning that a beacon says what is true now and a backlog would
// broadcast an ever-staler picture. that is right for the heartbeat and wrong for everything else
// on this link: a command ack is not state, it is an answer, and a heartbeat landing in the same
// instant threw it away. on the bench a command would go unacknowledged perhaps one time in five
// with the command itself having been received and executed - the worst possible failure, since
// the ground then retransmits something already done.
//
// three deep covers a heartbeat, an ack and a downlink progress frame arriving together, which is
// the worst real case, at 57 ms of air time each
constexpr size_t kBeaconDepth = 3;

struct beacon_slot_t {
    uint8_t frame[fsw::kFrameMaxSize];
    size_t len;
};

beacon_slot_t s_beacon[kBeaconDepth];
size_t s_beacon_head = 0U;       // next to transmit
size_t s_beacon_count = 0U;      // how many are waiting
uint32_t s_beacon_dropped = 0U;  // offered to a full queue and never transmitted

// the uart is passed rather than derived from the handle: until telemetry_task_create runs both
// handles are null, so any "which buffer is this" test would answer the same for both links
bool enqueue(StreamBufferHandle_t sb, uart_t* u, const uint8_t* data, size_t len) {
    if (len == 0U) {
        return true;
    }

    // before the scheduler nothing is draining these, so boot output goes straight to the wire.
    // that is the banner, the i2c scan and the sensor init results - all of it printed while the
    // board is still single-threaded, where a blocking write costs nothing
    if (sb == nullptr || xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        uart_write(u, data, len);
        return true;
    }

    bool ok = false;
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    // all or nothing - xStreamBufferSend will happily take a partial message, and a frame cut in
    // half is worse than no frame at all
    if (xStreamBufferSpacesAvailable(sb) >= len) {
        ok = xStreamBufferSend(sb, data, len, 0) == len;
    }
    if (!ok) {
        s_dropped++;
    }
    (void)xSemaphoreGive(s_lock);
    return ok;
}

// empty one link to its uart: wait up to `wait` for the first bytes, then take everything behind
// them. this is where the blocking lives, and it is deliberate - see the header
void drain(StreamBufferHandle_t sb, uart_t* u, TickType_t wait) {
    size_t n = xStreamBufferReceive(sb, s_drain, sizeof(s_drain), wait);
    while (n > 0U) {
        uart_write(u, s_drain, n);
        n = xStreamBufferReceive(sb, s_drain, sizeof(s_drain), 0);
    }
}

// a silent drop is the one outcome this project does not accept from a telemetry path. reported
// after the drain, so there is room for the line that says the buffer ran out of room
void report_drops(void) {
    static uint32_t last = 0;
    const uint32_t now = telemetry_out_dropped();
    if (now != last) {
        last = now;
        console_printf("TLM DROPPED %lu\r\n", static_cast<unsigned long>(now));
    }
}

// push the newest beacon frame. rfm95_send holds the transmit until it completes and puts the
// radio back into receive itself, so there is nothing to poll here and no window in which this
// task's loop period decides how long the vehicle stays deaf
void service_beacon(void) {
    if (s_beacon_count == 0U) {
        return;
    }

    uint8_t frame[fsw::kFrameMaxSize];
    size_t len = 0U;

    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    const beacon_slot_t& slot = s_beacon[s_beacon_head];
    len = slot.len;
    for (size_t i = 0U; i < len; i++) {
        frame[i] = slot.frame[i];
    }
    s_beacon_head = (s_beacon_head + 1U) % kBeaconDepth;
    s_beacon_count--;
    (void)xSemaphoreGive(s_lock);

    // outside the lock: this reaches for the spi3 bus, and holding two locks to send a beacon is
    // how a payload downlink ends up waiting on a radio
    (void)rfm95_send(frame, len);
}

void telemetry_task(void*) {
    for (;;) {
        // every telemetry frame goes to both links, so in practice the console is what wakes this
        // and the downlink drains in the same pass with nothing left waiting
        drain(s_console, uart_console, pdMS_TO_TICKS(kIdleWaitMs));
        drain(s_downlink, uart_downlink, 0);

        service_beacon();
        report_drops();
        task_health_checkin(TASK_ID_TELEMETRY);
    }
}

}  // namespace

bool telemetry_out_console(const uint8_t* data, size_t len) {
    return enqueue(s_console, uart_console, data, len);
}

// the poll gate: one message id the ground asked to see once, over the radio. a single volatile
// byte - written by the control task, read wherever frames flow - and the benign race (two frames
// of the same kind in one cycle both matching) costs one extra beacon at worst
static volatile uint8_t s_poll_id = 0U;

void telemetry_poll(uint8_t msg_id) { s_poll_id = msg_id; }

bool telemetry_out_downlink(const uint8_t* data, size_t len) {
    // frame layout: sync(2), id(1), len(1), payload, crc(2)
    if (s_poll_id != 0U && len > 2U && data[2] == s_poll_id) {
        s_poll_id = 0U;
        (void)telemetry_out_beacon(data, len);
    }
    return enqueue(s_downlink, uart_downlink, data, len);
}

bool telemetry_out_beacon(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0U || len > fsw::kFrameMaxSize || s_lock == nullptr) {
        return false;
    }

    bool ok = false;
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_beacon_count < kBeaconDepth) {
        beacon_slot_t& slot = s_beacon[(s_beacon_head + s_beacon_count) % kBeaconDepth];
        for (size_t i = 0U; i < len; i++) {
            slot.frame[i] = data[i];
        }
        slot.len = len;
        s_beacon_count++;
        ok = true;
    } else {
        s_beacon_dropped++;  // the link is behind; this one never makes it onto the air
    }
    (void)xSemaphoreGive(s_lock);
    return ok;
}

size_t telemetry_out_room(void) {
    if (s_console == nullptr || s_downlink == nullptr) {
        return 0U;
    }
    const size_t c = xStreamBufferSpacesAvailable(s_console);
    const size_t d = xStreamBufferSpacesAvailable(s_downlink);
    return (c < d) ? c : d;
}

uint32_t telemetry_out_dropped(void) { return s_dropped; }

uint32_t telemetry_out_beacon_dropped(void) { return s_beacon_dropped; }

void telemetry_task_create(void) {
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    s_console = xStreamBufferCreateStatic(sizeof(s_console_store), kTriggerBytes, s_console_store,
                                          &s_console_sb);
    s_downlink = xStreamBufferCreateStatic(sizeof(s_downlink_store), kTriggerBytes,
                                           s_downlink_store, &s_downlink_sb);

    const TaskHandle_t h = xTaskCreateStatic(telemetry_task, "telemetry", TASK_STACK_TELEMETRY,
                                             nullptr, TASK_PRIO_TELEMETRY, s_stack, &s_tcb);
    // the idle wait is 50 ms, but a full 1 KB buffer takes ~89 ms to push onto the wire and this
    // task is expected to sit inside uart_write for that long. 1000 ms is comfortably past the
    // worst legitimate pass without being so loose that a wedged task goes unnoticed
    task_health_register(TASK_ID_TELEMETRY, h, 1000U);
}
