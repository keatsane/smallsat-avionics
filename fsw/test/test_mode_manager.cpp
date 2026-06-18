#include <cstring>

#include "doctest.h"
#include "fsw/executive.hpp"
#include "fsw/mode_manager.hpp"

using namespace fsw;

// Drive the mode manager to the target mode
static void drive_to(ModeManager& mm, Mode target) {
    if (target == Mode::BOOT) {
        return;
    }

    if (target == Mode::SAFE) {  // SAFE isn't on the chain
        REQUIRE(mm.request(Mode::SAFE, Trigger::FaultEntry, 0, "REQ-FAULT-002"));
        return;
    }

    for (uint8_t m = 1; m <= static_cast<uint8_t>(target); ++m) {
        const Mode next = static_cast<Mode>(m);

        if (mm.mode() != next) {
            REQUIRE(mm.request(next, Trigger::Command, 0, "REQ-MODE-003"));
        }
    }
}

// Confirm transition anticipated legality matches true legality
static void expect_transition(Mode from, Trigger trigger, Mode to, bool legal) {
    ModeManager mm;

    CAPTURE(from);  // shown on failure so you know which case broke
    CAPTURE(trigger);
    CAPTURE(to);

    drive_to(mm, from);  // request the legal steps to reach `from`

    const auto n = mm.log().size();

    CHECK(mm.request(to, trigger, 0, "REQ-MODE-003") == legal);
    CHECK(mm.mode() == (legal ? to : from));        // changed iff legal
    CHECK(mm.log().size() == (legal ? n + 1 : n));  // logged iff legal
}

TEST_SUITE("MODE MANAGEMENT REQUIREMENTS") {
    TEST_CASE("REQ-MODE-001") {
        ModeManager mm;

        // the current mode is always exactly one of the six
        CHECK(static_cast<uint8_t>(mm.mode()) < kModeCount);
        for (Mode to : {Mode::STANDBY, Mode::DETUMBLE, Mode::POINTING, Mode::DOWNLINK,
                        Mode::STANDBY, Mode::SAFE}) {
            mm.request(to, Trigger::Command, 0, "REQ-MODE-001");
            CHECK(static_cast<uint8_t>(mm.mode()) < kModeCount);  // never an out-of-range value
        }
    }

    TEST_CASE("REQ-MODE-002") {
        ModeManager mm;

        CHECK(mm.mode() == Mode::BOOT);
        CHECK(mm.log().empty());
    }

    TEST_CASE("REQ-MODE-003") {
        SUBCASE("BOOT Transitions") {
            expect_transition(Mode::BOOT, Trigger::Nominal, Mode::BOOT, false);
            expect_transition(Mode::BOOT, Trigger::Nominal, Mode::STANDBY, true);
            expect_transition(Mode::BOOT, Trigger::Nominal, Mode::DETUMBLE, true);
            expect_transition(Mode::BOOT, Trigger::Nominal, Mode::POINTING, false);
            expect_transition(Mode::BOOT, Trigger::Nominal, Mode::DOWNLINK, false);
            expect_transition(Mode::BOOT, Trigger::Nominal, Mode::SAFE, true);
        }

        SUBCASE("STANDBY Transitions") {
            expect_transition(Mode::STANDBY, Trigger::Nominal, Mode::BOOT, false);
            expect_transition(Mode::STANDBY, Trigger::Nominal, Mode::STANDBY, false);
            expect_transition(Mode::STANDBY, Trigger::Nominal, Mode::DETUMBLE, true);
            expect_transition(Mode::STANDBY, Trigger::Nominal, Mode::POINTING, false);
            expect_transition(Mode::STANDBY, Trigger::Nominal, Mode::DOWNLINK, false);
            expect_transition(Mode::STANDBY, Trigger::Nominal, Mode::SAFE, true);
        }

        SUBCASE("DETUMBLE Transitions") {
            expect_transition(Mode::DETUMBLE, Trigger::Nominal, Mode::BOOT, false);
            expect_transition(Mode::DETUMBLE, Trigger::Nominal, Mode::STANDBY, true);
            expect_transition(Mode::DETUMBLE, Trigger::Nominal, Mode::DETUMBLE, false);
            expect_transition(Mode::DETUMBLE, Trigger::Nominal, Mode::POINTING, true);
            expect_transition(Mode::DETUMBLE, Trigger::Nominal, Mode::DOWNLINK, false);
            expect_transition(Mode::DETUMBLE, Trigger::Nominal, Mode::SAFE, true);
        }

        SUBCASE("POINTING Transitions") {
            expect_transition(Mode::POINTING, Trigger::Nominal, Mode::BOOT, false);
            expect_transition(Mode::POINTING, Trigger::Nominal, Mode::STANDBY, true);
            expect_transition(Mode::POINTING, Trigger::Nominal, Mode::DETUMBLE, false);
            expect_transition(Mode::POINTING, Trigger::Nominal, Mode::POINTING, false);
            expect_transition(Mode::POINTING, Trigger::Nominal, Mode::DOWNLINK, true);
            expect_transition(Mode::POINTING, Trigger::Nominal, Mode::SAFE, true);
        }

        SUBCASE("DOWNLINK Transitions") {
            expect_transition(Mode::DOWNLINK, Trigger::Nominal, Mode::BOOT, false);
            expect_transition(Mode::DOWNLINK, Trigger::Nominal, Mode::STANDBY, true);
            expect_transition(Mode::DOWNLINK, Trigger::Nominal, Mode::DETUMBLE, false);
            expect_transition(Mode::DOWNLINK, Trigger::Nominal, Mode::POINTING, true);
            expect_transition(Mode::DOWNLINK, Trigger::Nominal, Mode::DOWNLINK, false);
            expect_transition(Mode::DOWNLINK, Trigger::Nominal, Mode::SAFE, true);
        }

        SUBCASE("SAFE Transitions") {
            expect_transition(Mode::SAFE, Trigger::Nominal, Mode::BOOT, false);
            expect_transition(Mode::SAFE, Trigger::Nominal, Mode::STANDBY, false);
            expect_transition(Mode::SAFE, Trigger::Nominal, Mode::DETUMBLE, false);
            expect_transition(Mode::SAFE, Trigger::Nominal, Mode::POINTING, false);
            expect_transition(Mode::SAFE, Trigger::Nominal, Mode::DOWNLINK, false);
            expect_transition(Mode::SAFE, Trigger::Nominal, Mode::SAFE, false);
        }
    }

    TEST_CASE("REQ-MODE-004") {
        ModeManager mm;

        // an illegal request is rejected with no side effects: mode unchanged, nothing logged
        REQUIRE(mm.request(Mode::STANDBY, Trigger::Command, 0, "setup"));
        const Mode before = mm.mode();
        const auto n = mm.log().size();

        // illegal and out of range transitions
        CHECK_FALSE(mm.request(Mode::DOWNLINK, Trigger::Command, 0, "REQ-MODE-004"));
        CHECK_FALSE(mm.request(static_cast<Mode>(99), Trigger::Command, 0, "REQ-MODE-004"));

        CHECK(mm.mode() == before);   // rejected requests left the mode untouched
        CHECK(mm.log().size() == n);  // appended nothing
    }

    TEST_CASE("REQ-MODE-005") {
        ModeManager mm;

        for (uint8_t m = 0; m < kModeCount - 1; m++)  // loop through all except safe (- 1)
            expect_transition(static_cast<Mode>(m), Trigger::Command, Mode::SAFE, true);
    }

    TEST_CASE("REQ-MODE-006") {
        SUBCASE("Fail: Trigger != Command") {
            expect_transition(Mode::SAFE, Trigger::FaultCleared, Mode::STANDBY, false);
            expect_transition(Mode::SAFE, Trigger::FaultEntry, Mode::STANDBY, false);
            expect_transition(Mode::SAFE, Trigger::PowerOn, Mode::STANDBY, false);
            expect_transition(Mode::SAFE, Trigger::Timeout, Mode::STANDBY, false);
            expect_transition(Mode::SAFE, Trigger::Nominal, Mode::STANDBY, false);
        }

        SUBCASE("Fail: To != STANDBY") {
            expect_transition(Mode::SAFE, Trigger::Command, Mode::BOOT, false);
            expect_transition(Mode::SAFE, Trigger::Command, Mode::DETUMBLE, false);
            expect_transition(Mode::SAFE, Trigger::Command, Mode::POINTING, false);
            expect_transition(Mode::SAFE, Trigger::Command, Mode::DOWNLINK, false);
            expect_transition(Mode::SAFE, Trigger::Command, Mode::SAFE, false);
        }

        SUBCASE("Pass") { expect_transition(Mode::SAFE, Trigger::Command, Mode::STANDBY, true); }

        SUBCASE("Full end to end - only the commanded recovery leaves SAFE") {
            Executive exec;

            {
                Inputs inputs;
                // three bad samples clear UNDERVOLTAGE's debounce (3) -> latch -> SAFE
                inputs.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
                inputs.fault_updates.push_back({Fault::UNDERVOLTAGE, true});
                inputs.fault_updates.push_back({Fault::UNDERVOLTAGE, true});

                exec.cycle(inputs, 10);
            }

            CHECK(exec.modes().mode() == Mode::SAFE);
            CHECK(exec.faults().is_active(Fault::UNDERVOLTAGE));

            const auto mode_log_size_after_safe_entry = exec.modes().log().size();

            {
                Inputs inputs;
                inputs.command = command_t{static_cast<uint8_t>(Command::CLEAR_FAULT),
                                           static_cast<uint8_t>(Fault::UNDERVOLTAGE), 1};

                exec.cycle(inputs, 20);
            }

            REQUIRE(exec.commands().log().size() >= 1);
            CHECK(exec.commands().log().back().accepted);
            CHECK(exec.modes().mode() == Mode::SAFE);
            CHECK_FALSE(exec.faults().is_active(Fault::UNDERVOLTAGE));

            // CLEAR_FAULT should not itself leave SAFE or append a mode transition
            CHECK(exec.modes().log().size() == mode_log_size_after_safe_entry);

            {
                Inputs inputs;
                inputs.command = command_t{static_cast<uint8_t>(Command::SET_MODE),
                                           static_cast<uint8_t>(Mode::STANDBY), 2};

                exec.cycle(inputs, 30);
            }

            REQUIRE(exec.modes().log().size() == mode_log_size_after_safe_entry + 1);
            CHECK(exec.modes().mode() == Mode::STANDBY);
            CHECK(exec.modes().log().back().from == Mode::SAFE);
            CHECK(exec.modes().log().back().to == Mode::STANDBY);
            CHECK(exec.modes().log().back().trigger == Trigger::Command);
        }
    }

    TEST_CASE("REQ-MODE-007") {
        ModeManager mm;

        REQUIRE(mm.request(Mode::STANDBY, Trigger::PowerOn, 0, "REQ-MODE-007"));
        CHECK(mm.log().back().trigger == Trigger::PowerOn);

        REQUIRE(mm.request(Mode::DETUMBLE, Trigger::Command, 0, "REQ-MODE-007"));
        CHECK(mm.log().back().trigger == Trigger::Command);

        REQUIRE(mm.request(Mode::POINTING, Trigger::Nominal, 0, "REQ-MODE-007"));
        CHECK(mm.log().back().trigger == Trigger::Nominal);

        REQUIRE(mm.request(Mode::STANDBY, Trigger::FaultCleared, 0, "REQ-MODE-007"));
        CHECK(mm.log().back().trigger == Trigger::FaultCleared);

        REQUIRE(mm.request(Mode::DETUMBLE, Trigger::Timeout, 0, "REQ-MODE-007"));
        CHECK(mm.log().back().trigger == Trigger::Timeout);

        REQUIRE(mm.request(Mode::SAFE, Trigger::FaultEntry, 0, "REQ-MODE-007"));
        CHECK(mm.log().back().trigger == Trigger::FaultEntry);
    }

    TEST_CASE("REQ-MODE-008") {
        ModeManager mm;

        REQUIRE(mm.log().empty());

        REQUIRE(mm.request(Mode::STANDBY, Trigger::Command, 1234, "REQ-MODE-008"));

        REQUIRE(mm.log().size() == 1);
        CHECK(mm.log().back().t_ms == 1234);
        CHECK(mm.log().back().trigger == Trigger::Command);
        CHECK(mm.log().back().from == Mode::BOOT);
        CHECK(mm.log().back().to == Mode::STANDBY);
        CHECK(std::strcmp(mm.log().back().req_id, "REQ-MODE-008") == 0);
    }

    TEST_CASE("REQ-MODE-009") {
        ModeManager mm;

        REQUIRE(mm.log().empty());

        for (uint32_t m = 0; m < 50; m++) {
            REQUIRE(
                mm.request(static_cast<Mode>((m % 2) + 1), Trigger::Command, m, "REQ-MODE-009"));
        }

        REQUIRE(mm.log().size() == kLogCapacity);
        CHECK(mm.log().front().t_ms == (50 - mm.log().size()));
        CHECK(mm.log().back().t_ms == 49);
        CHECK(mm.log().back().from == Mode::STANDBY);
        CHECK(mm.log().front().from == Mode::DETUMBLE);
    }
}
