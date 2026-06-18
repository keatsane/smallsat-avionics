#include "doctest.h"
#include "fsw/executive.hpp"

using namespace fsw;

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

            // Fault was severe, so SAFE entry still happened.
            CHECK(exec.modes().mode() == Mode::SAFE);
            REQUIRE(exec.modes().log().size() == 1);
            CHECK(exec.modes().log().back().trigger == Trigger::FaultEntry);

            // CLEAR_FAULT was dispatched after SAFE entry.
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
}
