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

}  // namespace fsw::platform
