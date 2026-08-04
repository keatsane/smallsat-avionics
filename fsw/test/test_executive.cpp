#include <string>

#include "doctest.h"
#include "fsw/executive.hpp"

using namespace fsw;

// the host backend records these so dispatch is observable without a camera (platform_host.cpp)
namespace fsw::platform {
extern int capture_calls;
extern uint8_t capture_resolution;
extern uint8_t polled_msg_id;
extern bool payload_downlink_active;
extern float wheel_torque_nm;
}  // namespace fsw::platform

TEST_SUITE("EXECUTIVE REQUIREMENTS") {
    TEST_CASE("REQ-EXEC-001") {
        SUBCASE("Empty cycle is a no-op") {
            Executive exec;

            exec.cycle(Inputs{}, 10);

            CHECK(exec.modes().mode() == Mode::BOOT);
            CHECK(exec.modes().log().empty());
            CHECK(exec.commands().log().empty());
            CHECK(exec.faults().active() == 0u);
        }

        SUBCASE("Same-cycle CLEAR_FAULT is still dispatched after the SAFE entry") {
            Executive exec;
            Inputs inputs;

            // three bad samples clear UNDERVOLTAGE's debounce (3) -> latch -> SAFE
            inputs.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            inputs.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            inputs.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            inputs.command = command_t{static_cast<uint8_t>(Command::CLEAR_FAULT),
                                       static_cast<uint8_t>(Fault::UNDERVOLTAGE), 1};

            exec.cycle(inputs, 10);

            CHECK(exec.commands().log().back().accepted);

            // the fault was critical, so the safing still happened
            CHECK(exec.modes().mode() == Mode::SAFE);
            REQUIRE(exec.modes().log().size() == 1);
            CHECK(exec.modes().log().back().trigger == Trigger::FaultEntry);

            // and CLEAR_FAULT was dispatched after it
            CHECK_FALSE(exec.faults().is_active(Fault::UNDERVOLTAGE));
        }

        SUBCASE("Same-cycle SET_MODE does not override the SAFE entry") {
            Executive exec;

            Inputs inputs;
            // three bad samples clear UNDERVOLTAGE's debounce (3) -> latch -> SAFE
            inputs.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            inputs.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            inputs.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            inputs.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                       static_cast<uint8_t>(Mode::STANDBY), 1};

            exec.cycle(inputs, 10);

            // acceptance acknowledges validation only - the safing wins the cycle
            CHECK(exec.commands().log().back().accepted);
            CHECK(exec.modes().mode() == Mode::SAFE);
            REQUIRE(exec.modes().log().size() == 1);
            CHECK(exec.modes().log().back().trigger == Trigger::FaultEntry);
        }
    }

    TEST_CASE("REQ-MODE-010") {
        SUBCASE("BOOT is left for STANDBY once the debounce window has passed") {
            Executive exec;

            for (int i = 0; i < 3; ++i) {
                exec.cycle(Inputs{}, static_cast<uint32_t>(10 * i));
                CHECK(exec.modes().mode() == Mode::BOOT);  // still self-checking
            }

            exec.cycle(Inputs{}, 40);

            CHECK(exec.modes().mode() == Mode::STANDBY);
            REQUIRE(exec.modes().log().size() == 1);
            CHECK(exec.modes().log().back().trigger == Trigger::Nominal);
            CHECK(std::string(exec.modes().log().back().req_id) == "REQ-MODE-010");
        }

        SUBCASE("A critical fault during the self-check safes instead of proceeding") {
            Executive exec;

            Inputs bad;  // debounce is 3, so one cycle carries all three samples
            bad.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            bad.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            bad.fault_updates.push_back({Fault::UNDERVOLTAGE, true});

            for (int i = 0; i < 6; ++i) {
                exec.cycle(bad, static_cast<uint32_t>(10 * i));
            }

            CHECK(exec.modes().mode() == Mode::SAFE);  // never passed through STANDBY
            for (const ModeTransition& t : exec.modes().log()) {
                CHECK(t.to != Mode::STANDBY);
            }
        }

        SUBCASE("An inhibited critical fault does not hold the vehicle in BOOT") {
            Executive exec;
            exec.inhibit_fault(Fault::UNDERVOLTAGE);

            Inputs bad;
            bad.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            bad.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            bad.fault_updates.push_back({Fault::UNDERVOLTAGE, true});

            for (int i = 0; i < 6; ++i) {
                exec.cycle(bad, static_cast<uint32_t>(10 * i));
            }

            CHECK(exec.faults().is_active(Fault::UNDERVOLTAGE));  // latched and reported
            CHECK(exec.modes().mode() == Mode::STANDBY);          // but not acted on
        }
    }

    TEST_CASE("REQ-FAULT-005") {
        SUBCASE("WHEEL_DROPOUT retreats POINTING to STANDBY without safing") {
            Executive exec;

            // pointing is only reachable through detumble
            Inputs detumble;
            detumble.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                         static_cast<uint8_t>(Mode::DETUMBLE), 1};
            exec.cycle(detumble, 10);

            Inputs enter;
            enter.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                      static_cast<uint8_t>(Mode::POINTING), 2};
            exec.cycle(enter, 20);
            REQUIRE(exec.modes().mode() == Mode::POINTING);

            Inputs drop;  // debounce is 3, so one cycle carries all three samples
            drop.fault_updates.push_back({Fault::WHEEL_DROPOUT, true});
            drop.fault_updates.push_back({Fault::WHEEL_DROPOUT, true});
            drop.fault_updates.push_back({Fault::WHEEL_DROPOUT, true});
            exec.cycle(drop, 30);

            CHECK(exec.faults().is_active(Fault::WHEEL_DROPOUT));
            CHECK(exec.modes().mode() == Mode::STANDBY);  // degraded, so a retreat and not SAFE
            CHECK(exec.modes().log().back().trigger == Trigger::FaultEntry);
            CHECK(std::string(exec.modes().log().back().req_id) == "REQ-FAULT-005");
        }

        SUBCASE("WHEEL_DROPOUT outside the actuating modes only latches") {
            Executive exec;

            Inputs enter;
            enter.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                      static_cast<uint8_t>(Mode::STANDBY), 1};
            exec.cycle(enter, 10);
            const size_t transitions = exec.modes().log().size();

            Inputs drop;
            drop.fault_updates.push_back({Fault::WHEEL_DROPOUT, true});
            drop.fault_updates.push_back({Fault::WHEEL_DROPOUT, true});
            drop.fault_updates.push_back({Fault::WHEEL_DROPOUT, true});
            exec.cycle(drop, 20);

            CHECK(exec.faults().is_active(Fault::WHEEL_DROPOUT));
            CHECK(exec.modes().mode() == Mode::STANDBY);
            CHECK(exec.modes().log().size() == transitions);  // no transition logged
        }
    }

    TEST_CASE("REQ-MODE-012") {
        SUBCASE("a spinning vehicle detumbles itself and resumes the interrupted mode") {
            Executive exec;

            // DOWNLINK rather than POINTING: a pointing slew is body rate on purpose, so POINTING
            // is deliberately outside the autonomous entry - see the subcase below
            Inputs standby;
            standby.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                        static_cast<uint8_t>(Mode::STANDBY), 1};
            exec.cycle(standby, 100);
            Inputs downlink;
            downlink.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                         static_cast<uint8_t>(Mode::DOWNLINK), 2};
            exec.cycle(downlink, 200);
            REQUIRE(exec.modes().mode() == Mode::DOWNLINK);

            // a sustained high rate - three cycles of debounce, then the vehicle acts on its own
            uint32_t t = 300;
            for (int i = 0; i < 4; i++) {
                Inputs spin;
                spin.body_rate_rads = 1.0F;
                exec.cycle(spin, t);
                t += 100;
            }
            CHECK(exec.modes().mode() == Mode::DETUMBLE);
            CHECK(exec.modes().log().back().trigger == Trigger::Nominal);

            // the rate nulls; a second inside the deadband later it is back where it was
            for (int i = 0; i < 12; i++) {
                Inputs still;
                still.body_rate_rads = 0.0F;
                exec.cycle(still, t);
                t += 100;
            }
            CHECK(exec.modes().mode() == Mode::DOWNLINK);  // resumed, not parked in STANDBY
        }

        SUBCASE("POINTING is left alone - its rate is the manoeuvre, not a tumble") {
            // measured on the rig 2026-08-04: a slew passes the entry threshold long before it
            // reaches the bearing, so an autonomous entry cancels the manoeuvre that asked for
            // the rate, resumes POINTING, and starts the same fight over. forty seconds of that
            // was what the bench showed
            Executive exec;

            Inputs standby;
            standby.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                        static_cast<uint8_t>(Mode::STANDBY), 1};
            exec.cycle(standby, 100);
            Inputs pointing;
            pointing.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                         static_cast<uint8_t>(Mode::POINTING), 2};
            exec.cycle(pointing, 200);
            REQUIRE(exec.modes().mode() == Mode::POINTING);

            uint32_t t = 300;
            for (int i = 0; i < 10; i++) {
                Inputs slew;
                slew.body_rate_rads = 1.0F;  // well past detumble_enter_rads, and sustained
                exec.cycle(slew, t);
                t += 100;
            }

            CHECK(exec.modes().mode() == Mode::POINTING);
        }

        SUBCASE("a single bumped sample does not trigger the recovery") {
            Executive exec;
            Inputs standby;
            standby.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                        static_cast<uint8_t>(Mode::STANDBY), 1};
            exec.cycle(standby, 100);

            Inputs bump;
            bump.body_rate_rads = 1.0F;
            exec.cycle(bump, 200);
            Inputs still;
            still.body_rate_rads = 0.0F;
            exec.cycle(still, 300);

            CHECK(exec.modes().mode() == Mode::STANDBY);  // the debounce held
        }
    }

    TEST_CASE("PULSE_WHEEL is an actuator test point, bounded by the flight software") {
        // it exists so the plant can be characterised with nothing plugged into the vehicle - a
        // cable across the rotating joint is a spring, and a tethered measurement measures it
        SUBCASE("the pulse expires on its own rather than standing until cancelled") {
            Executive exec;
            Inputs standby;
            standby.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                        static_cast<uint8_t>(Mode::STANDBY), 1};
            exec.cycle(standby, 100);

            Inputs pulse;
            pulse.command = command_t{static_cast<uint8_t>(Command::PULSE_WHEEL), 20, 2};
            exec.cycle(pulse, 200);
            CHECK(platform::wheel_torque_nm == doctest::Approx(0.020F));

            Inputs idle;
            exec.cycle(idle, 400);  // still inside the window
            CHECK(platform::wheel_torque_nm == doctest::Approx(0.020F));

            exec.cycle(idle, 200 + kWheelPulseMs + 1);  // and past it
            CHECK(platform::wheel_torque_nm == doctest::Approx(0.0F));
        }

        SUBCASE("a negative argument is a pulse the other way") {
            Executive exec;
            Inputs standby;
            standby.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                        static_cast<uint8_t>(Mode::STANDBY), 1};
            exec.cycle(standby, 100);

            Inputs pulse;  // 0xF6 is -10 as a signed byte
            pulse.command = command_t{static_cast<uint8_t>(Command::PULSE_WHEEL), 0xF6, 2};
            exec.cycle(pulse, 200);

            CHECK(platform::wheel_torque_nm == doctest::Approx(-0.010F));
        }

        SUBCASE("leaving STANDBY abandons the pulse rather than leaving torque standing") {
            Executive exec;
            Inputs standby;
            standby.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                        static_cast<uint8_t>(Mode::STANDBY), 1};
            exec.cycle(standby, 100);

            Inputs pulse;
            pulse.command = command_t{static_cast<uint8_t>(Command::PULSE_WHEEL), 20, 2};
            exec.cycle(pulse, 200);
            REQUIRE(platform::wheel_torque_nm == doctest::Approx(0.020F));

            // a critical fault safes the vehicle mid-pulse
            Inputs bad;
            bad.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            bad.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            bad.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            exec.cycle(bad, 250);

            REQUIRE(exec.modes().mode() == Mode::SAFE);
            CHECK(platform::wheel_torque_nm == doctest::Approx(0.0F));
        }
    }

    TEST_CASE("momentum dumping ignores the wheel's standstill noise") {
        // the ESC's velocity estimate reads +/-3 rad/s with the wheel stopped, so a dump deadband
        // inside that fires on noise and spins the wheel up in whichever direction the noise
        // pointed. that left every manoeuvre starting from an arbitrary wheel state
        Executive exec;
        Inputs standby;
        standby.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                    static_cast<uint8_t>(Mode::STANDBY), 1};
        exec.cycle(standby, 100);
        REQUIRE(exec.modes().mode() == Mode::STANDBY);

        wheel_status_t noisy{};
        noisy.velocity_mrad_s = -3100;  // the bench's measured standstill reading
        noisy.flags = kWheelFlagFocReady;

        Inputs idle;
        idle.wheel = noisy;
        exec.cycle(idle, 200);
        CHECK(platform::wheel_torque_nm == doctest::Approx(0.0F));

        // a wheel that really is turning still gets unwound, and against its own direction
        wheel_status_t spinning = noisy;
        spinning.velocity_mrad_s = 20000;
        idle.wheel = spinning;
        exec.cycle(idle, 300);
        CHECK(platform::wheel_torque_nm < 0.0F);
    }

    TEST_CASE("REQ-TLM-006") {
        SUBCASE("a telemetry request reaches the platform with its message id") {
            Executive exec;

            Inputs poll;
            poll.command = command_t{static_cast<uint8_t>(Command::REQUEST_TELEMETRY), 0x08, 1};
            exec.cycle(poll, 100);

            CHECK(exec.commands().log().back().accepted);
            CHECK(platform::polled_msg_id == 0x08);
        }
    }

    TEST_CASE("REQ-PAY-001") {
        SUBCASE("An accepted CAPTURE_IMAGE starts a capture") {
            Executive exec;

            // imaging is legal in POINTING only, which is reachable through DETUMBLE
            Inputs detumble;
            detumble.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                         static_cast<uint8_t>(Mode::DETUMBLE), 1};
            exec.cycle(detumble, 10);

            Inputs pointing;
            pointing.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                         static_cast<uint8_t>(Mode::POINTING), 2};
            exec.cycle(pointing, 20);
            REQUIRE(exec.modes().mode() == Mode::POINTING);

            const int before = platform::capture_calls;
            Inputs inputs;
            inputs.command = command_t{static_cast<uint8_t>(Command::CAPTURE_IMAGE), 0, 3};
            exec.cycle(inputs, 30);

            CHECK(exec.commands().log().back().accepted);
            CHECK(platform::capture_calls == before + 1);
        }

        SUBCASE("A rejected CAPTURE_IMAGE starts nothing") {
            Executive exec;

            // drive SAFE first - imaging is not legal there, so the command is refused
            Inputs safing;
            safing.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            safing.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            safing.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            exec.cycle(safing, 10);
            REQUIRE(exec.modes().mode() == Mode::SAFE);

            const int before = platform::capture_calls;
            Inputs inputs;
            inputs.command = command_t{static_cast<uint8_t>(Command::CAPTURE_IMAGE), 0, 2};
            exec.cycle(inputs, 20);

            CHECK_FALSE(exec.commands().log().back().accepted);
            CHECK(platform::capture_calls == before);  // never reached the payload
        }

        SUBCASE("The requested size reaches the payload, and a bad one never does") {
            Executive exec;

            Inputs detumble;
            detumble.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                         static_cast<uint8_t>(Mode::DETUMBLE), 1};
            exec.cycle(detumble, 10);
            Inputs pointing;
            pointing.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                         static_cast<uint8_t>(Mode::POINTING), 2};
            exec.cycle(pointing, 20);
            REQUIRE(exec.modes().mode() == Mode::POINTING);

            Inputs big;
            big.command = command_t{static_cast<uint8_t>(Command::CAPTURE_IMAGE),
                                    static_cast<uint8_t>(ImageResolution::R1600x1200), 3};
            exec.cycle(big, 30);
            CHECK(exec.commands().log().back().accepted);
            CHECK(platform::capture_resolution ==
                  static_cast<uint8_t>(ImageResolution::R1600x1200));

            // a size the catalog does not have is refused at validation, so the camera is never
            // asked for it - the alternative is a sensor left in whatever state a bad table wrote
            const int before = platform::capture_calls;
            Inputs bad;
            bad.command =
                command_t{static_cast<uint8_t>(Command::CAPTURE_IMAGE), kResolutionCount, 4};
            exec.cycle(bad, 40);
            CHECK(exec.commands().log().back().reason == CmdReject::BadArg);
            CHECK(platform::capture_calls == before);
        }
    }

    TEST_CASE("REQ-PAY-002") {
        SUBCASE("CAMERA_DROPOUT latches without safing or changing mode") {
            Executive exec;

            Inputs enter;
            enter.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                      static_cast<uint8_t>(Mode::STANDBY), 1};
            exec.cycle(enter, 10);
            const size_t transitions = exec.modes().log().size();

            Inputs drop;
            drop.fault_updates.push_back({Fault::CAMERA_DROPOUT, true});
            drop.fault_updates.push_back({Fault::CAMERA_DROPOUT, true});
            drop.fault_updates.push_back({Fault::CAMERA_DROPOUT, true});
            exec.cycle(drop, 20);

            CHECK(exec.faults().is_active(Fault::CAMERA_DROPOUT));
            CHECK(exec.modes().mode() == Mode::STANDBY);  // a dead payload is not a safing event
            CHECK(exec.modes().log().size() == transitions);  // no transition logged
        }
    }

    TEST_CASE("REQ-PAY-004") {
        // DOWNLINK is only reachable from POINTING, so every case here walks the ladder first
        auto to_downlink = [](Executive& exec) {
            const Mode ladder[] = {Mode::STANDBY, Mode::DETUMBLE, Mode::POINTING, Mode::DOWNLINK};
            uint16_t seq = 0;
            uint32_t t = 10;
            for (const Mode m : ladder) {
                Inputs in;
                in.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                       static_cast<uint8_t>(m), ++seq};
                exec.cycle(in, t);
                t += 10;
            }
            return t;
        };

        // the fsw says whether to downlink, not how fast - the rate is the platform's, so what is
        // gradeable here is that the flag tracks the mode on every cycle rather than on an edge
        SUBCASE("the payload stream follows DOWNLINK in and out") {
            Executive exec;

            uint32_t t = to_downlink(exec);
            REQUIRE(exec.modes().mode() == Mode::DOWNLINK);
            CHECK(platform::payload_downlink_active);

            // still on, with no command at all - a level, not an edge, so a task that missed the
            // transition still learns the truth on the next cycle
            exec.cycle(Inputs{}, t);
            t += 10;
            CHECK(platform::payload_downlink_active);

            Inputs leave;
            leave.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                      static_cast<uint8_t>(Mode::STANDBY), 90};
            exec.cycle(leave, t);
            REQUIRE(exec.modes().mode() == Mode::STANDBY);
            CHECK_FALSE(platform::payload_downlink_active);
        }

        SUBCASE("a safing retreat out of DOWNLINK stops the stream") {
            Executive exec;

            const uint32_t t = to_downlink(exec);
            REQUIRE(platform::payload_downlink_active);

            // no explicit hook stops the downlink on safing - it stops because the flag is
            // recomputed from the mode every cycle, which is the reason to send a level
            Inputs safing;
            safing.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            safing.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            safing.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
            exec.cycle(safing, t);

            REQUIRE(exec.modes().mode() == Mode::SAFE);
            CHECK_FALSE(platform::payload_downlink_active);
        }
    }
}
