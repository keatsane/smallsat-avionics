/**
 * @file   platform_host.cpp
 * @brief  host backend of the platform layer for the unit tests - std clock, frames discarded
 */

#include <chrono>

#include "fsw/platform.hpp"

namespace fsw::platform {

uint32_t now_ms() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - t0).count());
}

void send_telemetry(const uint8_t* frame, uint32_t len) {
    // unit tests have nowhere to send a frame - emissions are SIL's to observe (the shim
    // backend prints them); swallowing keeps raw binary out of the doctest output
    (void)frame;
    (void)len;
}

}  // namespace fsw::platform
