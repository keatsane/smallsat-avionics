/**
 * @file   platform_stm32.cpp
 * @brief  stm32 backend for the flight software platform layer
 */

#include "devices/ov2640.h"
#include "downlink_task.hpp"
#include "drivers/uart.h"
#include "fsw/platform.hpp"
#include "protocol/frame.hpp"
#include "protocol/msg.hpp"
#include "telemetry_task.hpp"

namespace fsw::platform {

void send_telemetry(const uint8_t* frame, uint32_t len) {
    // queued, not written - this runs inside the control cycle, and the wire is 100x slower than
    // the decision that produced the frame
    (void)telemetry_out_console(frame, len);
    (void)telemetry_out_downlink(frame, len);
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

// the chunking itself lives in the downlink task - it owns the pacing, and this stays the thin
// boundary the fsw sees
void set_payload_downlink(bool active) { downlink_task_set_active(active); }

}  // namespace fsw::platform
