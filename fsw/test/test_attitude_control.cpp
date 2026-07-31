#include <cmath>

#include "doctest.h"
#include "fsw/attitude_control.hpp"

using namespace fsw;

TEST_SUITE("ATTITUDE CONTROL") {
    TEST_CASE("REQ-ADCS-001") {
        // written in terms of what the platform ends up doing rather than what the formula
        // says, and that is the whole point. the first closed-loop run had this law's sign
        // backwards - commanding a wheel torque whose reaction accelerated the platform it was
        // meant to stop - and a test asserting "returns -k * rate" would have passed happily. a
        // test has to know the wheel and the platform push opposite ways
        SUBCASE("the reaction opposes the rate, which means the command follows it") {
            AttitudeControl ac;

            const float cmd_pos = ac.detumble(1.0F);
            const float cmd_neg = ac.detumble(-1.0F);

            // the platform feels -cmd. spinning forwards must produce a backwards push on the
            // platform, so the commanded wheel torque is positive
            CHECK(cmd_pos > 0.0F);
            CHECK(-cmd_pos < 0.0F);  // spelled out: the reaction is what slows the platform

            CHECK(cmd_neg < 0.0F);
            CHECK(-cmd_neg > 0.0F);
        }

        SUBCASE("a faster spin is pushed harder") {
            AttitudeControl ac;
            CHECK(ac.detumble(2.0F) > ac.detumble(1.0F));
        }

        SUBCASE("inside the deadband it stops pushing") {
            AttitudeControl ac;
            const float inside = ac.gains().rate_deadband * 0.5F;

            CHECK(ac.detumble(inside) == doctest::Approx(0.0F));
            CHECK(ac.detumble(-inside) == doctest::Approx(0.0F));
            CHECK_FALSE(ac.saturated());  // and doing nothing is not saturating
        }

        SUBCASE("just outside the deadband it does push") {
            AttitudeControl ac;
            const float outside = ac.gains().rate_deadband * 1.5F;
            CHECK(std::fabs(ac.detumble(outside)) > 0.0F);
        }
    }

    TEST_CASE("REQ-ADCS-003") {
        SUBCASE("the command never exceeds the actuator limit") {
            AttitudeControl ac;
            const float huge = 1000.0F;  // a rate no bench rig will ever see

            CHECK(ac.detumble(huge) == doctest::Approx(ac.gains().torque_max));
            CHECK(ac.detumble(-huge) == doctest::Approx(-ac.gains().torque_max));
        }

        SUBCASE("hitting the limit is reported, not just silently clamped") {
            // the executive has to be able to see sustained saturation to raise a fault on it,
            // and inferring it from the torque it just asked for is guesswork
            AttitudeControl ac;

            ac.detumble(1000.0F);
            CHECK(ac.saturated());

            ac.detumble(ac.gains().rate_deadband * 1.5F);
            CHECK_FALSE(ac.saturated());
        }
    }

    TEST_CASE("REQ-ADCS-002") {
        SUBCASE("entering pointing takes the current heading as the target") {
            AttitudeControl ac;
            ac.point(1.0F, 0.5F);  // drift somewhere
            REQUIRE(std::fabs(ac.heading_error()) > 0.0F);

            ac.enter_pointing();

            CHECK(ac.heading_error() == doctest::Approx(0.0F));
            CHECK(ac.pointing_in_band());
        }

        SUBCASE("heading is integrated from the rate") {
            AttitudeControl ac;
            ac.enter_pointing();

            ac.point(0.5F, 0.2F);  // 0.5 rad/s for 0.2 s
            CHECK(ac.heading_error() == doctest::Approx(0.1F));

            ac.point(0.5F, 0.2F);
            CHECK(ac.heading_error() == doctest::Approx(0.2F));
        }

        SUBCASE("the reaction pushes back toward the target") {
            AttitudeControl ac;
            ac.enter_pointing();

            // drift a long way positive, well outside the band
            const float cmd = ac.point(1.0F, 1.0F);

            // same convention as detumble: the command follows the error so the platform feels
            // the opposite. a positive heading error must produce a negative push on the platform
            CHECK(cmd > 0.0F);
            CHECK(-cmd < 0.0F);
        }

        SUBCASE("inside the band it damps rate but stops chasing the angle") {
            ControlGains g;
            g.k_angle = 100.0F;  // absurd, so a chase would be unmistakable
            g.k_damp = 0.0F;     // and nothing else can contribute
            g.torque_max = 100.0F;
            AttitudeControl ac(g);
            ac.enter_pointing();

            const float cmd = ac.point(0.0F, 0.0F);  // zero error, zero rate

            REQUIRE(ac.pointing_in_band());
            CHECK(cmd == doctest::Approx(0.0F));
        }

        SUBCASE("outside the band it does correct") {
            AttitudeControl ac;
            ac.enter_pointing();
            ac.point(0.0F, 0.0F);
            REQUIRE(ac.pointing_in_band());

            // walk the heading past the band edge
            ac.point(ac.gains().pointing_band * 4.0F, 1.0F);

            CHECK_FALSE(ac.pointing_in_band());
            CHECK(std::fabs(ac.point(0.0F, 0.0F)) > 0.0F);
        }
    }

    TEST_CASE("gains are configurable, because they will be retuned on the rig") {
        ControlGains g;
        g.k_rate = 0.5F;
        g.torque_max = 10.0F;
        g.rate_deadband = 0.0F;
        AttitudeControl ac(g);

        CHECK(ac.detumble(1.0F) == doctest::Approx(0.5F));
    }
}
