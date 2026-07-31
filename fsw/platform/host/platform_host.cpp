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

void set_wheel_torque(int16_t torque_mv) {
    // no wheel on the host - the control law is what the unit tests grade
    (void)torque_mv;
}

// no camera on the host, so the backend counts the calls instead - dispatch is the thing the
// unit tests can actually grade (REQ-PAY-001). the tests declare this extern themselves
int capture_calls = 0;

void capture_image(void) { capture_calls++; }

// no payload on the host, so the backend records the flag rather than moving bytes - whether the
// executive asked is the part the unit tests can grade (REQ-PAY-004). the tests declare it extern
bool payload_downlink_active = false;

void set_payload_downlink(bool active) { payload_downlink_active = active; }

}  // namespace fsw::platform
