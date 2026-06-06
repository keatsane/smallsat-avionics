// host backend of the platform layer - used for SIL and the unit tests. the target
// backend (firmware drivers) lives with the firmware and is linked instead on the board.

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
