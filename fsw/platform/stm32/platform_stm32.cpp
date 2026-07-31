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
#include "protocol/state.hpp"
#include "telemetry_task.hpp"

namespace fsw::platform {

void send_telemetry(const uint8_t* frame, uint32_t len) {
    // queued, not written - this runs inside the control cycle, and the wire is 100x slower than
    // the decision that produced the frame
    (void)telemetry_out_console(frame, len);
    (void)telemetry_out_downlink(frame, len);

    // the radio takes the heartbeat and nothing else, which is an air-time fact rather than a
    // preference. one 21-byte heartbeat at SF7/BW125 is ~57 ms on the air; at 1 Hz that is 5.7%
    // duty and comfortable. the full stream is about five frames per 100 ms cycle, which would
    // want 2.8 seconds of air time per second of flight - not a tuning problem, an impossible one.
    //
    // so LoRa carries the beacon and the command acks, and nothing else. bulk data goes on the
    // nRF24, which is the dual-link split the architecture asked for (REQ-TLM-004). an ack is 11
    // bytes and only happens when the ground actually commands something, so it costs nothing
    // against the budget - and without it the ground commands blind, which is worse than not
    // commanding at all.
    //
    // byte 2 is the message id: the layout is sync(2), id(1), len(1), payload, crc(2)
    // AttitudeStatus joins them because the ground station draws a dial from it, and a dial with
    // no data is a circle. it is 19 bytes at the heartbeat's cadence, so it costs one more beacon
    // slot per second and takes the link from about 6% of air time to about 11%
    if (len >= kFrameOverhead && (frame[2] == static_cast<uint8_t>(MsgId::Heartbeat) ||
                                  frame[2] == static_cast<uint8_t>(MsgId::CommandAck) ||
                                  frame[2] == static_cast<uint8_t>(MsgId::AttitudeStatus))) {
        (void)telemetry_out_beacon(frame, len);
    }
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

void capture_image(uint8_t resolution) {
    // the catalog id maps to this sensor's tables here, on the backend side of the boundary. the
    // enums are declared in the same order deliberately, and the static_assert is what keeps that
    // from being a coincidence somebody breaks later
    static_assert(static_cast<uint8_t>(OV2640_RES_COUNT) == fsw::kResolutionCount,
                  "the camera's sizes and FSW_RESOLUTION_LIST have drifted apart");

    // an out-of-range size leaves the sensor where it was rather than refusing the capture - the
    // command handler rejects bad arguments before they reach here, so this is belt and braces
    if (resolution < fsw::kResolutionCount) {
        (void)ov2640_set_resolution(static_cast<ov2640_res_t>(resolution));
    }

    // a refusal here is not silent - the camera's state comes back through camera_data_t, so a
    // capture that never starts shows up as a frame that never appears
    (void)ov2640_capture_start();
}

// the chunking itself lives in the downlink task - it owns the pacing, and this stays the thin
// boundary the fsw sees
void set_payload_downlink(bool active) { downlink_task_set_active(active); }

}  // namespace fsw::platform
