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

// torque -> q-axis volts for this motor. SimpleFOC's voltage torque mode drives V_q, and the
// current that follows is V_q/R, so torque = V_q * kt / R and the inverse is V_q = torque * R/kt.
// GBM4108-120T: 12.4 ohm windings (bom.md), kt estimated at ~0.1 N m/A for this frame size.
// ESTIMATE - the one number here that wants measuring against the rig, by commanding a known
// voltage and fitting the platform's response once the ESC link is back
constexpr float kOhmsPerKt = 124.0F;  // R/kt, volts per N m

void set_wheel_torque_nm(float torque_nm) {
    static uint16_t seq = 0U;

    const float mv = torque_nm * kOhmsPerKt * 1000.0F;
    const float clamped = (mv > 32767.0F) ? 32767.0F : ((mv < -32768.0F) ? -32768.0F : mv);

    wheel_command_t c{};
    c.torque_mv = static_cast<int16_t>(clamped);
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
