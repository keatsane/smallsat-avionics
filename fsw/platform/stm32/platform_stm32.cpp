/**
 * @file   platform_stm32.cpp
 * @brief  stm32 backend for the flight software platform layer
 */

#include "devices/ov2640.h"
#include "drivers/uart.h"
#include "fsw/platform.hpp"
#include "protocol/frame.hpp"
#include "protocol/msg.hpp"

namespace fsw::platform {

void send_telemetry(const uint8_t* frame, uint32_t len) {
    uart_write(uart_console, frame, len);
    uart_write(uart_downlink, frame, len);
}

void set_wheel_torque(int16_t torque_mv) {
    static uint16_t seq = 0U;

    wheel_command_t c{};
    c.torque_mv = torque_mv;
    c.seq = ++seq;

    uint8_t buf[kFrameMaxSize];
    const size_t n = frame_encode(static_cast<uint8_t>(MsgId::WheelCommand),
                                  reinterpret_cast<const uint8_t*>(&c), sizeof(c), buf);
    uart_write(uart_esc, buf, n);
}

void capture_image(void) {
    // a refusal here is not silent - the camera's state comes back through camera_data_t, so a
    // capture that never starts shows up as a frame that never appears
    (void)ov2640_capture_start();
}

bool send_payload_chunk(void) {
    static uint16_t image_id = 0U;  // increments per image, so two never interleave silently
    static uint16_t chunk = 0U;
    static uint16_t chunks = 0U;  // 0 means no image in flight

    if (!ov2640_frame_ready()) {
        chunks = 0U;  // frame gone (discarded, or a new capture started) - drop the bookkeeping
        return false;
    }

    if (chunks == 0U) {  // first chunk of a new image - fix its shape before any of it goes out
        const uint32_t remaining = ov2640_frame_bytes();
        if (remaining == 0U) {
            return false;
        }
        image_id++;
        chunk = 0U;
        chunks = static_cast<uint16_t>((remaining + kPayloadChunkBytes - 1U) / kPayloadChunkBytes);
    }

    payload_data_t p{};
    p.image_id = image_id;
    p.chunk = chunk;
    p.chunks = chunks;
    p.len = static_cast<uint8_t>(ov2640_read_chunk(p.data, kPayloadChunkBytes));
    if (p.len == 0U) {
        chunks = 0U;  // drained early - the frame ended, so the image is over either way
        return false;
    }

    uint8_t buf[kFrameMaxSize];
    const size_t n = frame_encode(static_cast<uint8_t>(MsgId::PayloadData),
                                  reinterpret_cast<const uint8_t*>(&p), sizeof(p), buf);
    uart_write(uart_console, buf, n);
    uart_write(uart_downlink, buf, n);

    if (++chunk >= chunks) {
        chunks = 0U;  // whole image sent
    }
    return true;
}

}  // namespace fsw::platform
