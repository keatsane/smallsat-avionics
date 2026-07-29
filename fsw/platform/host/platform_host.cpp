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

bool send_payload_chunk(void) {
    return false;  // no payload on the host - there is never anything waiting
}

}  // namespace fsw::platform
