/**
 * @file   platform_host.cpp
 * @brief  host backend of the platform layer for the unit tests - frames discarded
 */

#include "fsw/platform.hpp"

namespace fsw::platform {

void send_telemetry(const uint8_t* frame, uint32_t len) {
    // unit tests have nowhere to send a frame - emissions are SIL's to observe (the shim
    // backend prints them); swallowing keeps raw binary out of the doctest output
    (void)frame;
    (void)len;
}

// no wheel on the host, so the backend records what was asked for - the commanded torque is
// exactly what the unit tests grade about the control law (REQ-ADCS-001)
float wheel_torque_nm = 0.0F;

void set_wheel_torque_nm(float torque_nm) { wheel_torque_nm = torque_nm; }

// no camera on the host, so the backend counts the calls instead - dispatch is the thing the
// unit tests can actually grade (REQ-PAY-001). the tests declare these extern themselves
int capture_calls = 0;
uint8_t capture_resolution = 0;

void capture_image(uint8_t resolution) {
    capture_calls++;
    capture_resolution = resolution;
}

// recorded so the dispatch is gradeable, like the capture calls above
uint8_t polled_msg_id = 0;

void poll_telemetry(uint8_t msg_id) { polled_msg_id = msg_id; }

// no payload on the host, so the backend records the flag rather than moving bytes - whether the
// executive asked is the part the unit tests can grade (REQ-PAY-004). the tests declare it extern
bool payload_downlink_active = false;

void set_payload_downlink(bool active) { payload_downlink_active = active; }

}  // namespace fsw::platform
