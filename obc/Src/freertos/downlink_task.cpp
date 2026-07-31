/**
 * @file   downlink_task.cpp
 * @brief  reads the payload fifo and puts chunks on both links
 *
 * The payload downlink is selective repeat: the image goes out once, the ground names the chunks
 * it is missing over the LoRa uplink (MsgId::ChunkRequest), and this task resends exactly those.
 * An earlier version sent every image three times blind - it worked, but it spent two full image
 * transmissions to cover losses that averaged a tenth of one, and it could still lose. Naming the
 * gaps costs one small uplink frame per round and converges on exactly the missing set.
 */

#include "downlink_task.hpp"

#include "FreeRTOS.h"
#include "devices/nrf24.h"
#include "devices/ov2640.h"
#include "drivers/systick.h"
#include "protocol/frame.hpp"
#include "protocol/msg.hpp"
#include "queue.h"
#include "rtos_tasks.h"
#include "task.h"
#include "task_health.hpp"
#include "telemetry_task.hpp"

namespace {

// how long to sleep between fills. short enough that the output buffers never sit empty while an
// image is waiting, long enough that a task at this priority is not spinning on a full buffer
constexpr TickType_t kPollMs = 20;

// pause between chunks while an image is moving.
//
// the link's slowest part is not the air, it is the ground station's per-packet work: a 70-byte
// frame is three nRF24 packets, the receiver's fifo holds three, and anything it cannot collect in
// time is gone. unpaced, 588 packets went out and 379 arrived - and because the losses come in
// runs rather than scattered, a frame that spans three packets almost never survived one. 196
// chunks produced a single decoded frame.
//
// so the transmitter waits instead. this costs downlink time on every link, wired ones included,
// and that is the trade: a slower stream that arrives beats a faster one that does not
constexpr TickType_t kChunkGapMs = 4;

// the largest image the request tracking can address: 4096 chunks x 56 bytes is ~229 KB, well
// past anything the ov2640 produces at UXGA. one bit per chunk, 512 bytes of bss
constexpr uint16_t kMaxChunks = 4096;

StaticTask_t s_tcb;
StackType_t s_stack[TASK_STACK_DOWNLINK];

// written by the control task each cycle, read here. a single aligned bool, so no lock is earned -
// the same reasoning the health task's check-in slots use
volatile bool s_active = false;

// bookkeeping for the image currently going out. only this task touches it
uint16_t s_image_id = 0;  // increments per image, so two never interleave silently
uint16_t s_chunk = 0;
uint16_t s_chunks = 0;        // 0 means no first pass in flight
uint16_t s_chunks_total = 0;  // survives the end of the pass, so requests can be range-checked
uint32_t s_capture_seen = 0;  // which capture the current image id belongs to

// ground requests cross from the uplink task through this queue, one (image_id << 16 | chunk)
// word per entry - a queue rather than a shared bitmap so no lock spans the two tasks. a full
// queue drops the overflow: the ground repeats its request until the image completes, so a
// dropped index costs one round trip and nothing else
constexpr UBaseType_t kReqQueueDepth = 96;
QueueHandle_t s_req_queue;
StaticQueue_t s_req_queue_buf;
uint8_t s_req_queue_storage[kReqQueueDepth * sizeof(uint32_t)];

// which chunks the ground has asked for again, one bit each. only this task touches it
uint32_t s_req_bits[kMaxChunks / 32U];
uint16_t s_req_count = 0;

// how often the progress frame goes out. faster while an image is actually moving, because a
// 6 KB image is gone in about two seconds and a 1 Hz report showed the transfer exactly never -
// the whole burst finished inside one period, and the only frame the ground ever saw said "idle".
// 500 ms costs about 11% of the beacon's air time, and only during a pass
constexpr uint32_t kStatusActiveMs = 500;
constexpr uint32_t kStatusIdleMs = 1000;
uint32_t s_next_status_ms = 0;

bool req_bit(uint16_t chunk) { return (s_req_bits[chunk / 32U] & (1UL << (chunk % 32U))) != 0U; }

void req_clear(uint16_t chunk) { s_req_bits[chunk / 32U] &= ~(1UL << (chunk % 32U)); }

void req_clear_all(void) {
    for (uint32_t& w : s_req_bits) {
        w = 0U;
    }
    s_req_count = 0U;
}

// say how far the downlink has got, on the *other* radio.
//
// the chunks ride the nRF24, which is transmit-only and cannot tell the ground anything about
// itself - so when an image never arrives there is no way to tell a vehicle that never started
// from a radio that dropped every packet. this frame answers that, and doubles as the progress
// bar for the long full-resolution downlinks
void send_status(void) {
    fsw::downlink_status_t d{};
    d.t_ms = millis();
    // zeros while nothing is moving. holding the last image's numbers left a bar sitting at
    // 100% for as long as the vehicle stayed in DOWNLINK, which reads as a stall rather than as
    // a finished pass
    const bool in_flight = (s_chunks != 0U) || (s_req_count != 0U);
    d.image_id = s_image_id;
    d.chunk = in_flight ? s_chunk : 0U;
    d.chunks = in_flight ? s_chunks_total : 0U;
    d.radio_sent = nrf24_sent();
    const uint32_t dropped = nrf24_dropped();
    d.radio_dropped = (dropped > 0xFFFFU) ? 0xFFFFU : static_cast<uint16_t>(dropped);

    uint8_t buf[fsw::kFrameMaxSize];
    const size_t n = fsw::frame_encode(static_cast<uint8_t>(fsw::MsgId::DownlinkStatus),
                                       reinterpret_cast<const uint8_t*>(&d), sizeof(d), buf);
    (void)telemetry_out_console(buf, n);
    (void)telemetry_out_downlink(buf, n);

    // one radio, not both - the ground station forwards everything it decodes, so sending this on
    // both put two identical lines on the console every second.
    //
    // which one depends on what is happening. while an image is moving the report belongs on the
    // LoRa beacon: the nRF24 is saturated with the payload, and adding to it would slow the thing
    // being reported on. while nothing is moving it belongs on the nRF24, where it is the link
    // check that lets the payload radio be tested without capturing an image first
    if (in_flight) {
        (void)telemetry_out_beacon(buf, n);
    } else {
        (void)nrf24_send(buf, n);
    }
}

// emit a progress frame if one is due. the period depends on whether an image is moving, so this
// is asked rather than scheduled by the caller
void service_status(void) {
    const uint32_t now = millis();
    if (!s_active || (int32_t)(now - s_next_status_ms) < 0) {
        return;
    }
    const bool in_flight = (s_chunks != 0U) || (s_req_count != 0U);
    send_status();
    s_next_status_ms = now + (in_flight ? kStatusActiveMs : kStatusIdleMs);
}

// encode one chunk and put it on every link. the fifo read must already have happened - this is
// the shared tail of the first pass and the resend sweep
void emit_chunk(uint16_t chunk, const uint8_t* data, uint8_t len) {
    fsw::payload_data_t p{};
    p.image_id = s_image_id;
    p.chunk = chunk;
    p.chunks = s_chunks_total;
    p.len = len;
    for (uint8_t i = 0U; i < len; i++) {
        p.data[i] = data[i];
    }

    uint8_t buf[fsw::kFrameMaxSize];
    const size_t n = fsw::frame_encode(static_cast<uint8_t>(fsw::MsgId::PayloadData),
                                       reinterpret_cast<const uint8_t*>(&p), sizeof(p), buf);
    (void)telemetry_out_console(buf, n);
    (void)telemetry_out_downlink(buf, n);

    // and over the air on the high-rate radio - this is the traffic the nrf24 exists for, and the
    // reason DOWNLINK is a mode rather than a label. called from this task and not the control
    // task because it reaches for the spi3 bus, and this is the one that should wait for it.
    // a full radio fifo drops the packet rather than blocking; the wired links still carry it
    (void)nrf24_send(buf, n);
}

// take everything waiting in the request queue into the bitmap. stale image ids are dropped here,
// so a request that raced a new capture asks for nothing
void drain_requests(void) {
    uint32_t word = 0U;
    while (xQueueReceive(s_req_queue, &word, 0) == pdTRUE) {
        const uint16_t id = static_cast<uint16_t>(word >> 16);
        const uint16_t chunk = static_cast<uint16_t>(word & 0xFFFFU);
        if (id != s_image_id || chunk >= s_chunks_total || chunk >= kMaxChunks) {
            continue;
        }
        if (!req_bit(chunk)) {
            s_req_bits[chunk / 32U] |= 1UL << (chunk % 32U);
            s_req_count++;
        }
    }
}

// put one chunk of the first pass on the links; false when there is nothing left to send
bool send_chunk(void) {
    if (!ov2640_frame_ready()) {
        s_chunks = 0U;  // frame gone (discarded, or a new capture started) - drop the bookkeeping
        return false;
    }

    if (s_chunks == 0U) {  // first chunk of a new image - fix its shape before any of it goes out
        // only a genuinely new capture starts a first pass. the frame stays readable in the fifo
        // after a pass so requests can be served from it, and without this check a rewound frame
        // would relaunch itself as a brand-new image id every time the task looped
        if (ov2640_capture_count() == s_capture_seen) {
            return false;
        }
        const uint32_t remaining = ov2640_frame_bytes();
        if (remaining == 0U) {
            return false;
        }
        s_capture_seen = ov2640_capture_count();
        s_image_id++;
        s_chunk = 0U;
        s_next_status_ms = millis();  // due immediately, so a pass opens with 0 of N
        s_chunks = static_cast<uint16_t>((remaining + fsw::kPayloadChunkBytes - 1U) /
                                         fsw::kPayloadChunkBytes);
        s_chunks_total = s_chunks;
        req_clear_all();  // requests for the previous image die with it
    }

    uint8_t data[fsw::kPayloadChunkBytes];
    const uint8_t len = static_cast<uint8_t>(ov2640_read_chunk(data, fsw::kPayloadChunkBytes));
    if (len == 0U) {
        s_chunks = 0U;  // drained early - the frame ended, so the image is over either way
        return false;
    }

    emit_chunk(s_chunk, data, len);

    if (++s_chunk >= s_chunks) {
        s_chunks = 0U;  // first pass done; anything else the ground asks for by name
    }
    return true;
}

// resend the chunks the ground named. the fifo is sequential, so this rewinds once and sweeps the
// whole frame, transmitting only the requested chunks - reading past the others costs
// microseconds each, and a sweep in order needs no random access the hardware does not have
void serve_requests(void) {
    if (s_req_count == 0U || s_chunks != 0U || !ov2640_rewind()) {
        return;
    }

    for (uint16_t idx = 0U; idx < s_chunks_total && s_active; idx++) {
        uint8_t data[fsw::kPayloadChunkBytes];
        const uint8_t len = static_cast<uint8_t>(ov2640_read_chunk(data, fsw::kPayloadChunkBytes));
        if (len == 0U) {
            break;  // frame ended early - stop rather than invent chunks
        }
        if (!req_bit(idx)) {
            continue;
        }

        // wait for room rather than dropping: these chunks have already been lost once, and a
        // resend that can itself be dropped on the floor never converges
        while (s_active && telemetry_out_room() < fsw::kFrameMaxSize) {
            task_health_checkin(TASK_ID_DOWNLINK);
            vTaskDelay(pdMS_TO_TICKS(kPollMs));
        }
        if (!s_active) {
            break;  // left DOWNLINK mid-sweep; the bits stay set for the next entry
        }

        emit_chunk(idx, data, len);
        req_clear(idx);
        s_req_count--;
        s_chunk = idx;  // so the progress frame shows the sweep moving

        task_health_checkin(TASK_ID_DOWNLINK);
        service_status();
        vTaskDelay(pdMS_TO_TICKS(kChunkGapMs));
    }
}

void downlink_task(void*) {
    for (;;) {
        // fill both links while there is room. checking for room first is what makes this
        // self-pacing: no chunks-per-cycle constant, and nothing is ever handed to a buffer that
        // would have to drop it.
        //
        // this loop does NOT simply fill and exit, which is how it was first written and why the
        // watchdog went unfed on the bench (2026-07-30). the telemetry task drains the buffers
        // while this one fills them, so room keeps reappearing and the loop runs until the whole
        // image is gone. so the check-in belongs inside: a pass here has no bounded duration, and
        // the task is alive and working the entire time. it still stops reporting if send_chunk
        // itself wedges, which is the case the deadline exists to catch
        while (s_active && telemetry_out_room() >= fsw::kFrameMaxSize) {
            if (!send_chunk()) {
                break;  // no image waiting, or the first pass just finished
            }
            task_health_checkin(TASK_ID_DOWNLINK);

            // reported from inside the loop, because this loop does not exit until the image is
            // gone. reporting only after it meant the first progress frame a downlink ever sent
            // was the one saying it had finished
            service_status();
            vTaskDelay(pdMS_TO_TICKS(kChunkGapMs));
        }

        // then whatever the ground says it is missing
        drain_requests();
        serve_requests();

        // and while parked in DOWNLINK with nothing moving, where it is the payload link's
        // keepalive - the radio stays silent in every other mode, which is what makes DOWNLINK a
        // mode rather than a label
        service_status();

        task_health_checkin(TASK_ID_DOWNLINK);
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
    }
}

}  // namespace

void downlink_task_set_active(bool active) { s_active = active; }

void downlink_task_request(const fsw::chunk_request_t& req) {
    if (s_req_queue == nullptr) {
        return;
    }
    const uint8_t n = (req.count > fsw::kChunkRequestMax) ? fsw::kChunkRequestMax : req.count;
    for (uint8_t i = 0U; i < n; i++) {
        const uint32_t word = (static_cast<uint32_t>(req.image_id) << 16) | req.chunks[i];
        (void)xQueueSend(s_req_queue, &word, 0);  // full queue drops - the ground asks again
    }
}

void downlink_task_create(void) {
    s_req_queue =
        xQueueCreateStatic(kReqQueueDepth, sizeof(uint32_t), s_req_queue_storage, &s_req_queue_buf);

    const TaskHandle_t h = xTaskCreateStatic(downlink_task, "downlink", TASK_STACK_DOWNLINK,
                                             nullptr, TASK_PRIO_DOWNLINK, s_stack, &s_tcb);
    // it checks in per chunk, not per pass - a pass lasts as long as the image does. so the
    // deadline is sized against one chunk's worth of work plus the 20 ms idle sleep, and 500 ms is
    // far past anything legitimate
    task_health_register(TASK_ID_DOWNLINK, h, 500U);
}
