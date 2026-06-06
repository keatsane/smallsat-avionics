#include "doctest.h"
#include "fsw/fault_manager.hpp"

using namespace fsw;

TEST_CASE("a fault latches and clears, and shows up in the bitmask") {
    FaultManager fm;
    CHECK_FALSE(fm.is_active(Fault::UNDERVOLTAGE));

    fm.set(Fault::UNDERVOLTAGE);
    CHECK(fm.is_active(Fault::UNDERVOLTAGE));
    CHECK(fm.active() == (1u << static_cast<unsigned>(Fault::UNDERVOLTAGE)));

    fm.clear(Fault::UNDERVOLTAGE);
    CHECK_FALSE(fm.is_active(Fault::UNDERVOLTAGE));
    CHECK(fm.active() == 0u);
}

TEST_CASE("different faults occupy different bits") {
    FaultManager fm;
    fm.set(Fault::UNDERVOLTAGE);
    fm.set(Fault::GYRO_DROPOUT);
    CHECK(fm.is_active(Fault::UNDERVOLTAGE));
    CHECK(fm.is_active(Fault::GYRO_DROPOUT));
    CHECK_FALSE(fm.is_active(Fault::OVERCURRENT));
}

TEST_CASE("undervoltage tracks the sample (no debounce yet)") {
    FaultManager fm;
    fm.update_undervoltage(true, 100);
    CHECK(fm.is_active(Fault::UNDERVOLTAGE));
    fm.update_undervoltage(false, 200);
    CHECK_FALSE(fm.is_active(Fault::UNDERVOLTAGE));
}

// TODO(phase2): debounce - a lone bad sample must NOT latch; N consecutive over a window does.
// then should_enter_safe() drives POINTING->SAFE, tied to REQ-FAULT-002
