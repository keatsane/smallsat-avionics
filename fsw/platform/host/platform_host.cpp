/**
 * @file   platform_host.cpp
 * @brief  host backend of the platform layer - std clock and stdout, for SIL and unit tests
 */

#include <chrono>
#include <cstdio>

#include "fsw/platform.hpp"

namespace fsw::platform {

uint32_t now_ms() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - t0).count());
}

void send_telemetry(const uint8_t* frame, uint32_t len) {
    // dump the framed bytes to stdout for the SIL runner / host decoder to pick up
    fwrite(frame, 1, len, stdout);
}

}  // namespace fsw::platform
