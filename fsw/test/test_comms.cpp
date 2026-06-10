#include <cstring>

#include "doctest.h"
#include "fsw/comms/command_handler.hpp"
#include "fsw/comms/telemetry_producer.hpp"
#include "fsw/executive.hpp"
#include "protocol/frame.hpp"
#include "protocol/msg.hpp"

using namespace fsw;

// build an Inputs carrying just one command
static Inputs command_input(Command c, uint8_t arg, uint16_t seq) {
    Inputs inputs;
    inputs.command = command_t{static_cast<uint8_t>(c), arg, seq};
    return inputs;
}

// feed a byte stream into a parser, return how many valid frames completed
static size_t count_frames(frame_parser_t& p, const uint8_t* data, size_t n) {
    size_t frames = 0;
    for (size_t i = 0; i < n; i++) {
        if (frame_decode(&p, data[i])) {
            frames++;
        }
    }
    return frames;
}

TEST_SUITE("COMMS REQUIREMENTS") {
    TEST_CASE("REQ-CMD-001") {
        SUBCASE("Legal, well-formed command is accepted") {
            CommandHandler ch;

            command_t cmd{static_cast<uint8_t>(Command::NOOP), 0, 1};

            CHECK(ch.handle(cmd, Mode::STANDBY, 0).accepted);
        }

        SUBCASE("Unknown id is rejected") {
            CommandHandler ch;

            command_t cmd{kCommandCount, 0, 2};  // one past the last real id

            CHECK(ch.handle(cmd, Mode::STANDBY, 0).reason == CmdReject::UnknownId);
        }

        SUBCASE("CAPTURE_IMAGE is legal only while POINTING") {
            CommandHandler ch;

            command_t cmd{static_cast<uint8_t>(Command::CAPTURE_IMAGE), 0, 3};

            CHECK(ch.handle(cmd, Mode::STANDBY, 0).reason == CmdReject::IllegalInMode);
            CHECK(ch.handle(cmd, Mode::POINTING, 0).accepted);
        }

        SUBCASE("Out-of-range arg is rejected") {
            CommandHandler ch;

            command_t set_mode{static_cast<uint8_t>(Command::SET_MODE), kModeCount, 4};

            CHECK(ch.handle(set_mode, Mode::STANDBY, 0).reason == CmdReject::BadArg);

            command_t clear_fault{static_cast<uint8_t>(Command::CLEAR_FAULT), kFaultCount, 5};

            CHECK(ch.handle(clear_fault, Mode::STANDBY, 0).reason == CmdReject::BadArg);
        }

        SUBCASE("Rejections are logged with their reason") {
            CommandHandler ch;

            command_t bad{kCommandCount, 0, 1};
            ch.handle(bad, Mode::STANDBY, 5);

            REQUIRE(ch.log().size() == 1);
            CHECK(ch.log().back().accepted == false);
            CHECK(ch.log().back().reason == CmdReject::UnknownId);
            CHECK(ch.log().back().t_ms == 5);

            command_t good{static_cast<uint8_t>(Command::NOOP), 0, 2};
            ch.handle(good, Mode::STANDBY, 6);

            REQUIRE(ch.log().size() == 2);
            CHECK(ch.log().back().reason == CmdReject::Ok);
        }

        SUBCASE("Every catalog row has a requirement and at least one legal mode") {
            CommandHandler ch;

            for (uint8_t id = 0; id < kCommandCount; ++id) {
                CAPTURE(id);
                const auto& spec = ch.command_spec(static_cast<Command>(id));
                REQUIRE(spec.req_id != nullptr);
                CHECK(std::strlen(spec.req_id) > 0);
                CHECK(spec.legal_modes != 0);
            }
        }

        SUBCASE("Command log is a ring that overwrites its oldest entry") {
            CommandHandler ch;

            for (uint32_t t = 0; t < 50; ++t) {
                command_t cmd{static_cast<uint8_t>(Command::NOOP), 0, static_cast<uint16_t>(t)};
                ch.handle(cmd, Mode::STANDBY, t);
            }

            REQUIRE(ch.log().size() == kLogCapacity);
            CHECK(ch.log().back().t_ms == 49);
            CHECK(ch.log().front().t_ms == 50 - kLogCapacity);
        }

        SUBCASE("Full end to end - accepted command transitions with Trigger::Command") {
            Executive exec;

            exec.cycle(command_input(Command::SET_MODE, static_cast<uint8_t>(Mode::STANDBY), 1),
                       10);

            REQUIRE(exec.commands().log().size() == 1);
            CHECK(exec.commands().log().back().accepted == true);

            REQUIRE(exec.modes().log().size() == 1);
            CHECK(exec.modes().log().back().trigger == Trigger::Command);
        }

        SUBCASE("Full end to end - rejected command changes nothing") {
            Executive exec;

            exec.cycle(command_input(Command::SET_MODE, kModeCount, 1), 10);

            REQUIRE(exec.commands().log().size() == 1);
            CHECK(exec.commands().log().back().accepted == false);
            CHECK(exec.modes().log().empty());
        }

        SUBCASE("Full end to end - illegal-in-mode is rejected through the executive") {
            Executive exec;

            exec.cycle(command_input(Command::CAPTURE_IMAGE, 0, 1), 10);  // BOOT, not POINTING

            REQUIRE(exec.commands().log().size() == 1);
            CHECK(exec.commands().log().back().reason == CmdReject::IllegalInMode);
            CHECK(exec.modes().log().empty());
        }

        SUBCASE("Full end to end - NOOP is accepted with no side effects") {
            Executive exec;

            exec.cycle(command_input(Command::NOOP, 0, 1), 10);

            REQUIRE(exec.commands().log().size() == 1);
            CHECK(exec.commands().log().back().accepted);
            CHECK(exec.modes().log().empty());
            CHECK(exec.faults().active() == 0u);
        }

        SUBCASE("Full end to end - CAPTURE_IMAGE causes no mode change") {
            Executive exec;

            exec.cycle(command_input(Command::SET_MODE, static_cast<uint8_t>(Mode::STANDBY), 1),
                       10);
            exec.cycle(command_input(Command::SET_MODE, static_cast<uint8_t>(Mode::DETUMBLE), 2),
                       20);
            exec.cycle(command_input(Command::SET_MODE, static_cast<uint8_t>(Mode::POINTING), 3),
                       30);
            REQUIRE(exec.modes().mode() == Mode::POINTING);
            const auto n = exec.modes().log().size();

            exec.cycle(command_input(Command::CAPTURE_IMAGE, 0, 4), 40);

            CHECK(exec.commands().log().back().accepted);
            CHECK(exec.modes().mode() == Mode::POINTING);
            CHECK(exec.modes().log().size() == n);
        }
    }

    TEST_CASE("REQ-CMD-002") {
        SUBCASE("Edge testing") {
            CommandHandler ch;

            ch.handle(command_t{static_cast<uint8_t>(Command::NOOP), 0, 0}, Mode::BOOT, 0);

            CHECK_FALSE(ch.link_lost(kCommandTimeoutMs));
            CHECK(ch.link_lost(kCommandTimeoutMs + 1));
        }

        SUBCASE("Any command resets the clock - even a rejected one") {
            CommandHandler ch;

            ch.handle(command_t{static_cast<uint8_t>(Command::NOOP), 0, 0}, Mode::BOOT, 0);

            // a garbage command arrives late - still proof the ground is transmitting
            ch.handle(command_t{kCommandCount, 0, 1}, Mode::BOOT, 4000);

            CHECK_FALSE(ch.link_lost(4000 + kCommandTimeoutMs));
            CHECK(ch.link_lost(4000 + kCommandTimeoutMs + 1));
        }

        SUBCASE("Boot edge - a silent ground trips the timer with no command ever heard") {
            CommandHandler ch;

            CHECK_FALSE(ch.link_lost(kCommandTimeoutMs));
            CHECK(ch.link_lost(kCommandTimeoutMs + 1));
        }

        SUBCASE("Full end to end") {
            Executive exec;
            {
                Inputs inputs;
                inputs.command = command_t{static_cast<uint8_t>(Command::NOOP), 0, 0};
                exec.cycle(inputs, 0);
            }

            {
                Inputs inputs;
                exec.cycle(inputs, kCommandTimeoutMs + 1);
            }

            CHECK(exec.faults().is_active(Fault::COMMAND_LINK_LOSS));
            CHECK(exec.modes().mode() == Mode::SAFE);
        }
    }

    TEST_CASE("REQ-CMD-003") {
        SUBCASE("Accepted command produces a matching ack") {
            TelemetryProducer tp;
            command_t cmd{static_cast<uint8_t>(Command::NOOP), 0, 10};
            CommandEvent ce{0, cmd.cmd_id, true, CmdReject::Ok};

            command_ack_t ack = tp.ack(cmd, ce);

            CHECK(ack.cmd_id == cmd.cmd_id);
            CHECK(static_cast<uint16_t>(ack.seq) == 10);
            CHECK(static_cast<bool>(ack.accepted) == true);
            CHECK(static_cast<CmdReject>(ack.reason) == CmdReject::Ok);
        }

        SUBCASE("Rejected command produces a matching ack") {
            TelemetryProducer tp;
            command_t cmd{kCommandCount, 0, 10};
            CommandEvent ce{0, cmd.cmd_id, false, CmdReject::UnknownId};

            command_ack_t ack = tp.ack(cmd, ce);

            CHECK(ack.cmd_id == cmd.cmd_id);
            CHECK(static_cast<uint16_t>(ack.seq) == 10);
            CHECK(static_cast<bool>(ack.accepted) == false);
            CHECK(static_cast<CmdReject>(ack.reason) == CmdReject::UnknownId);
        }
    }

    TEST_CASE("REQ-TLM-001") {
        SUBCASE("crc16/ccitt check value") {
            const uint8_t v[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
            CHECK(crc16(v, sizeof(v)) == 0x29B1);
        }

        SUBCASE("A corrupted frame is discarded and the stream resyncs") {
            heartbeat_t hb{};
            hb.seq = 1;

            uint8_t bad[kFrameMaxSize];
            size_t n = frame_encode(static_cast<uint8_t>(MsgId::Heartbeat),
                                    reinterpret_cast<const uint8_t*>(&hb), sizeof(hb), bad);
            bad[5] ^= 0xFF;  // flip a payload byte - crc must catch it

            frame_parser_t p{};
            CHECK(count_frames(p, bad, n) == 0);  // corrupted frame produces nothing

            uint8_t good[kFrameMaxSize];
            n = frame_encode(static_cast<uint8_t>(MsgId::Heartbeat),
                             reinterpret_cast<const uint8_t*>(&hb), sizeof(hb), good);
            CHECK(count_frames(p, good, n) == 1);  // same parser recovers on the next clean frame
        }

        SUBCASE("An oversized length byte forces a resync") {
            const uint8_t junk[] = {kFrameSync0, kFrameSync1, 0x01, 0xFF};  // len 255 > max

            frame_parser_t p{};
            CHECK(count_frames(p, junk, sizeof(junk)) == 0);

            heartbeat_t hb{};
            uint8_t good[kFrameMaxSize];
            size_t n = frame_encode(static_cast<uint8_t>(MsgId::Heartbeat),
                                    reinterpret_cast<const uint8_t*>(&hb), sizeof(hb), good);
            CHECK(count_frames(p, good, n) == 1);
        }

        SUBCASE("Back-to-back frames decode independently") {
            heartbeat_t hb{};
            uint8_t stream[2 * kFrameMaxSize];
            size_t n = frame_encode(static_cast<uint8_t>(MsgId::Heartbeat),
                                    reinterpret_cast<const uint8_t*>(&hb), sizeof(hb), stream);
            n += frame_encode(static_cast<uint8_t>(MsgId::Heartbeat),
                              reinterpret_cast<const uint8_t*>(&hb), sizeof(hb), &stream[n]);

            frame_parser_t p{};
            CHECK(count_frames(p, stream, n) == 2);
        }

        SUBCASE("Sync bytes inside a payload do not confuse the parser") {
            const uint8_t payload[] = {kFrameSync0, kFrameSync1, kFrameSync0, kFrameSync1};

            uint8_t frame[kFrameMaxSize];
            size_t n = frame_encode(0x42, payload, sizeof(payload), frame);

            frame_parser_t p{};
            std::optional<frame_t> got;
            for (size_t i = 0; i < n; i++) {
                if (auto f = frame_decode(&p, frame[i])) {
                    got = f;
                }
            }

            REQUIRE(got.has_value());
            CHECK(got->len == sizeof(payload));
            CHECK(std::memcmp(got->payload, payload, sizeof(payload)) == 0);
        }
    }

    TEST_CASE("REQ-TLM-002") {
        SUBCASE("Wire layout round-trips through encode + decode") {
            // the same wire format tools/frames.py decodes on the host
            heartbeat_t hb{};
            hb.uptime_ms = 12345;
            hb.mode = 3;
            hb.faults = 0;
            hb.seq = 7;

            uint8_t frame[kFrameMaxSize];
            size_t n = frame_encode(static_cast<uint8_t>(MsgId::Heartbeat),
                                    reinterpret_cast<const uint8_t*>(&hb), sizeof(hb), frame);
            REQUIRE(n == kFrameOverhead + sizeof(hb));

            frame_parser_t p{};
            std::optional<frame_t> got;
            for (size_t i = 0; i < n; i++) {
                if (auto f = frame_decode(&p, frame[i])) {
                    got = f;
                }
            }

            REQUIRE(got.has_value());
            CHECK(got->msg_id == static_cast<uint8_t>(MsgId::Heartbeat));
            CHECK(got->len == sizeof(hb));
            CHECK(std::memcmp(got->payload, &hb, sizeof(hb)) == 0);
        }

        SUBCASE("Heartbeat carries the cycle state") {
            TelemetryProducer tp;

            heartbeat_t hb = tp.heartbeat(1234, Mode::POINTING, fault_bit(Fault::UNDERVOLTAGE));

            CHECK(uint32_t{hb.uptime_ms} == 1234);
            CHECK(hb.mode == static_cast<uint8_t>(Mode::POINTING));
            CHECK(uint32_t{hb.faults} == fault_bit(Fault::UNDERVOLTAGE));
            CHECK(uint16_t{hb.seq} == 0);  // first heartbeat
        }

        SUBCASE("Due exactly one interval after the last heartbeat") {
            TelemetryProducer tp;

            // the first heartbeat is due one interval after boot, not at t=0
            CHECK_FALSE(tp.heartbeat_due(kHeartbeatIntervalMs - 1));
            CHECK(tp.heartbeat_due(kHeartbeatIntervalMs));

            tp.heartbeat(kHeartbeatIntervalMs, Mode::BOOT, 0);

            CHECK_FALSE(tp.heartbeat_due(2 * kHeartbeatIntervalMs - 1));
            CHECK(tp.heartbeat_due(2 * kHeartbeatIntervalMs));
        }
    }

    TEST_CASE("REQ-TLM-003") {
        TelemetryProducer tp;

        // the sequence increments by exactly one per heartbeat, so the ground can count drops
        for (uint16_t i = 0; i < 5; i++) {
            heartbeat_t hb = tp.heartbeat(i, Mode::BOOT, 0);
            CHECK(uint16_t{hb.seq} == i);
        }
    }
}
