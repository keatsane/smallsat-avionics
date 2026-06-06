// proves the shared C frame/msg code compiles, links (extern "C"), and runs from C++ -
// the same codec the firmware uses on the wire and tools/frames.py decodes on the host

#include <cstring>

#include "doctest.h"
#include "protocol/frame.h"
#include "protocol/msg.h"

TEST_CASE("crc16/ccitt check value") {
    const uint8_t v[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(crc16(v, sizeof(v)) == 0x29B1);
}

TEST_CASE("a heartbeat round-trips through encode + decode") {
    heartbeat_t hb{};
    hb.uptime_ms = 12345;
    hb.mode = 3;
    hb.faults = 0;
    hb.seq = 7;

    uint8_t frame[FRAME_MAX_SIZE];
    size_t n =
        frame_encode(MSG_HEARTBEAT, reinterpret_cast<const uint8_t*>(&hb), sizeof(hb), frame);
    REQUIRE(n == FRAME_OVERHEAD + sizeof(hb));

    frame_parser_t p{};
    frame_t out{};
    bool got = false;
    for (size_t i = 0; i < n; i++) {
        if (frame_decode(&p, frame[i], &out)) {
            got = true;
        }
    }

    REQUIRE(got);
    CHECK(out.msg_id == MSG_HEARTBEAT);
    CHECK(out.len == sizeof(hb));
    CHECK(std::memcmp(out.payload, &hb, sizeof(hb)) == 0);
}
