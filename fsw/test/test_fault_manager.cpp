#include <cstring>

#include "doctest.h"
#include "fsw/executive.hpp"
#include "fsw/fault_manager.hpp"

using namespace fsw;

TEST_SUITE("FAULT MANAGEMENT REQUIREMENTS") {
    TEST_CASE("REQ-FAULT-001") {
        FaultManager fm;

        fm.set(Fault::UNDERVOLTAGE, 0);  // latch directly

        CHECK(fm.is_active(Fault::UNDERVOLTAGE));

        fm.update(Fault::UNDERVOLTAGE, false, 0);  // condition clears on its own
        CHECK(fm.is_active(Fault::UNDERVOLTAGE));  // must NOT un-latch

        fm.clear(Fault::UNDERVOLTAGE, 0);  // only an explicit clear removes it

        CHECK_FALSE(fm.is_active(Fault::UNDERVOLTAGE));
    }

    TEST_CASE("REQ-FAULT-002") {
        SUBCASE("Transient never forces SAFE, the debounced latch does") {
            FaultManager fm;

            fm.update(Fault::UNDERVOLTAGE, true, 0);

            CHECK_FALSE(fm.is_active(Fault::UNDERVOLTAGE));
            CHECK_FALSE(fm.should_enter_safe());  // one bad sample (transient) must NOT force SAFE

            fm.update(Fault::UNDERVOLTAGE, true, 0);
            fm.update(Fault::UNDERVOLTAGE, true, 0);

            CHECK(fm.is_active(Fault::UNDERVOLTAGE));
            CHECK(fm.should_enter_safe());  // only after the full window

            fm.clear(Fault::UNDERVOLTAGE, 0);

            CHECK_FALSE(fm.should_enter_safe());

            fm.set(Fault::ACCEL_GYRO_DROPOUT,
                   0);  // degraded -> documented fallback, must NOT force SAFE

            CHECK_FALSE(fm.should_enter_safe());
        }

        SUBCASE("Every table row's severity drives should_enter_safe") {
            for (uint8_t id = 0; id < kFaultCount; ++id) {
                CAPTURE(id);
                FaultManager fm;
                const Fault f = static_cast<Fault>(id);

                fm.set(f, 0);

                CHECK(fm.should_enter_safe() == (fm.fault_spec(f).severity == Severity::Critical));
            }
        }

        SUBCASE("Full end to end") {
            Executive exec;
            Inputs inputs;

            inputs.fault_updates.push_back({Fault::WATCHDOG_TIMEOUT, true});

            exec.cycle(inputs, 10);
            CHECK(exec.modes().mode() == Mode::SAFE);
            REQUIRE(exec.modes().log().size() == 1);
            CHECK(exec.modes().log().back().trigger == Trigger::FaultEntry);
            CHECK(std::strlen(exec.modes().log().back().req_id) > 0);
        }
    }

    TEST_CASE("REQ-FAULT-003") {
        FaultManager fm;

        for (uint8_t id = 0; id < kFaultCount; ++id) {
            CAPTURE(id);  // names the offending fault on failure
            const auto& spec = fm.fault_spec(static_cast<Fault>(id));
            CHECK(spec.debounce_n >= 1);      // a 0-sample threshold is meaningless
            REQUIRE(spec.req_id != nullptr);  // every fault traces to an owning requirement
            CHECK(std::strlen(spec.req_id) > 0);
        }
    }

    TEST_CASE("REQ-FAULT-004") {
        SUBCASE("Latches only at the threshold") {
            FaultManager fm;

            fm.update(Fault::UNDERVOLTAGE, true, 0);  // one lone bad sample = transient

            CHECK_FALSE(fm.is_active(Fault::UNDERVOLTAGE));

            fm.update(Fault::UNDERVOLTAGE, true, 0);  // still only 2 of 3 - below threshold

            CHECK_FALSE(fm.is_active(Fault::UNDERVOLTAGE));

            fm.update(Fault::UNDERVOLTAGE, true, 0);  // 3 of 3 - latch

            REQUIRE(fm.log().size() == 1);
            CHECK(fm.log().back().fault == Fault::UNDERVOLTAGE);
            CHECK(fm.log().back().latched == true);
        }

        SUBCASE("Debounce streaks are independent per fault") {
            FaultManager fm;

            // both at 2 of 3
            fm.update(Fault::UNDERVOLTAGE, true, 0);
            fm.update(Fault::OVERVOLTAGE, true, 0);
            fm.update(Fault::UNDERVOLTAGE, true, 1);
            fm.update(Fault::OVERVOLTAGE, true, 1);

            fm.update(Fault::UNDERVOLTAGE, false, 2);  // breaks only this streak
            fm.update(Fault::OVERVOLTAGE, true, 2);    // 3rd consecutive - latches

            CHECK(fm.is_active(Fault::OVERVOLTAGE));
            CHECK_FALSE(fm.is_active(Fault::UNDERVOLTAGE));
        }

        SUBCASE("Full end to end - a transient causes no transition") {
            Executive exec;
            Inputs inputs;

            inputs.fault_updates.push_back({Fault::UNDERVOLTAGE, true});

            exec.cycle(inputs, 10);

            CHECK(exec.modes().log().empty());
        }
    }

    TEST_CASE("REQ-FAULT-005") {
        FaultManager fm;

        fm.set(Fault::SENSOR_DISAGREEMENT, 0);  // warning

        CHECK(fm.is_active(Fault::SENSOR_DISAGREEMENT));
        CHECK_FALSE(fm.should_enter_safe());

        fm.set(Fault::ACCEL_GYRO_DROPOUT, 0);  // degraded

        CHECK(fm.is_active(Fault::ACCEL_GYRO_DROPOUT));
        CHECK_FALSE(fm.should_enter_safe());

        // tbd degraded fallback behaviour

        // tbd all faults latched and reported in telemetry
    }

    TEST_CASE("REQ-FAULT-006") {
        SUBCASE("Most conservative response wins") {
            FaultManager fm;

            fm.set(Fault::ACCEL_GYRO_DROPOUT, 0);  // Degraded -> would fail over
            fm.set(Fault::UNDERVOLTAGE, 0);        // Critical -> demands SAFE

            CHECK(fm.should_enter_safe());  // SAFE is most conservative -> it wins

            fm.clear(Fault::UNDERVOLTAGE, 0);

            CHECK_FALSE(fm.should_enter_safe());  // critical cleared -> not demanding SAFE
            CHECK(fm.is_active(Fault::ACCEL_GYRO_DROPOUT));  // the degraded one never forced SAFE
        }

        SUBCASE("Full end to end - repeated cycles never duplicate the SAFE transition") {
            Executive exec;

            {
                Inputs inputs;
                inputs.fault_updates.push_back({Fault::WATCHDOG_TIMEOUT, true});

                exec.cycle(inputs, 10);
            }

            REQUIRE(exec.modes().mode() == Mode::SAFE);
            REQUIRE(exec.modes().log().size() == 1);
            CHECK(exec.modes().log().back().from == Mode::BOOT);

            {
                Inputs inputs;
                inputs.fault_updates.push_back({Fault::WATCHDOG_TIMEOUT, true});
                inputs.fault_updates.push_back({Fault::WATCHDOG_TIMEOUT, true});

                exec.cycle(inputs, 20);
            }

            CHECK(exec.modes().log().size() == 1);
            CHECK(exec.modes().log().back().from == Mode::BOOT);
        }
    }

    TEST_CASE("REQ-FAULT-008") {
        FaultManager fm;

        fm.set(Fault::ACTUATOR_SATURATION, 0);
        fm.set(Fault::ACCEL_GYRO_DROPOUT, 0);
        fm.set(Fault::UNDERVOLTAGE, 0);

        // check against expected bitmask
        CHECK(fm.active() ==
              (fault_bit(Fault::ACTUATOR_SATURATION) | fault_bit(Fault::ACCEL_GYRO_DROPOUT) |
               fault_bit(Fault::UNDERVOLTAGE)));
    }

    TEST_CASE("REQ-FAULT-009") {
        FaultManager fm;

        // debounce number of 3 for undervoltage
        fm.update(Fault::UNDERVOLTAGE, true, 0);
        fm.update(Fault::UNDERVOLTAGE, true, 0);

        CHECK_FALSE(fm.is_active(Fault::UNDERVOLTAGE));

        // send one good sample, should reset count
        fm.update(Fault::UNDERVOLTAGE, false, 0);
        fm.update(Fault::UNDERVOLTAGE, true, 0);

        CHECK_FALSE(fm.is_active(Fault::UNDERVOLTAGE));
    }

    TEST_CASE("REQ-FAULT-010") {
        FaultManager fm;

        fm.set(Fault::UNDERVOLTAGE, 0);
        for (uint32_t t = 1; t <= 50; ++t) {
            fm.update(Fault::UNDERVOLTAGE, false, t);  // a long healthy stretch
        }

        CHECK(fm.is_active(Fault::UNDERVOLTAGE));  // no autonomous clear, ever

        fm.clear(Fault::UNDERVOLTAGE, 99);  // only the explicit action works

        CHECK_FALSE(fm.is_active(Fault::UNDERVOLTAGE));
    }

    TEST_CASE("REQ-FAULT-011") {
        SUBCASE("Entry verifications") {
            FaultManager fm;

            fm.set(Fault::UNDERVOLTAGE, 0);

            REQUIRE(fm.log().size() == 1);
            CHECK(fm.log().back().t_ms == 0);
            CHECK(fm.log().back().latched == true);
            CHECK(fm.log().back().fault == Fault::UNDERVOLTAGE);

            fm.set(Fault::BUS_FAULT, 10);

            REQUIRE(fm.log().size() == 2);
            CHECK(fm.log().back().t_ms == 10);
            CHECK(fm.log().back().latched == true);
            CHECK(fm.log().back().fault == Fault::BUS_FAULT);

            fm.clear(Fault::UNDERVOLTAGE, 20);

            REQUIRE(fm.log().size() == 3);
            CHECK(fm.log().back().t_ms == 20);
            CHECK(fm.log().back().latched == false);
            CHECK(fm.log().back().fault == Fault::UNDERVOLTAGE);
        }

        SUBCASE("Edges only") {
            FaultManager fm;

            fm.update(Fault::UNDERVOLTAGE, true, 0);
            fm.update(Fault::UNDERVOLTAGE, true, 0);

            CHECK(fm.log().empty());  // sub-threshold samples: NO entries

            fm.update(Fault::UNDERVOLTAGE, true, 0);  // crosses threshold

            CHECK(fm.log().size() == 1);  // exactly one latch edge

            fm.update(Fault::UNDERVOLTAGE, true, 0);  // still bad, already latched

            CHECK(fm.log().size() == 1);  // no duplicate edge
        }

        SUBCASE("Redundant set/clear add no edges") {
            FaultManager fm;

            fm.set(Fault::UNDERVOLTAGE, 0);
            fm.set(Fault::UNDERVOLTAGE, 1);  // already latched - no edge

            REQUIRE(fm.log().size() == 1);

            fm.clear(Fault::UNDERVOLTAGE, 2);
            fm.clear(Fault::UNDERVOLTAGE, 3);  // already clear - no edge

            REQUIRE(fm.log().size() == 2);

            fm.clear(Fault::BUS_FAULT, 4);  // never latched - no edge

            CHECK(fm.log().size() == 2);
        }

        SUBCASE("Ring overflow") {
            FaultManager fm;

            for (uint32_t t = 0; t < 80; ++t) {
                if (t % 2 == 0) {
                    fm.set(Fault::UNDERVOLTAGE, t);  // even -> latch
                } else {
                    fm.clear(Fault::UNDERVOLTAGE, t);  // odd  -> clear
                }
            }

            REQUIRE(fm.log().size() == kLogCapacity);  // capped at capacity
            CHECK(fm.log().back().t_ms == 79);         // newest kept
            CHECK(fm.log().front().t_ms ==
                  80 - kLogCapacity);  // oldest 48 dropped -> front is t_ms 48
        }
    }
}
