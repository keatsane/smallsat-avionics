#include "doctest.h"
#include "fsw/mode_manager.hpp"

using namespace fsw;

TEST_CASE("mode manager starts in BOOT with an empty log") {
    ModeManager mm;
    CHECK(mm.mode() == Mode::BOOT);
    CHECK(mm.log().empty());
}

TEST_CASE("a transition updates the mode and records the log row") {
    ModeManager mm;
    mm.request(Mode::STANDBY, Trigger::PowerOn, 10, "REQ-MODE-001");

    CHECK(mm.mode() == Mode::STANDBY);
    REQUIRE(mm.log().size() == 1);

    const ModeTransition& t = mm.log()[0];
    CHECK(t.t_ms == 10);
    CHECK(t.trigger == Trigger::PowerOn);
    CHECK(t.from == Mode::BOOT);
    CHECK(t.to == Mode::STANDBY);
}

// TODO(phase2): illegal transitions are rejected; POINTING->SAFE on an undervoltage latch;
// the full mode table from docs/requirements.md
