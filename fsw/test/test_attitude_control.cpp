#include <cmath>
#include <optional>

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
            ac.update(1.0F, 0.5F, std::nullopt);  // drift somewhere
            REQUIRE(std::fabs(ac.heading()) > 0.0F);

            ac.enter_pointing();

            CHECK(ac.heading_error() == doctest::Approx(0.0F));
            CHECK(ac.pointing_in_band());
        }

        SUBCASE("heading is integrated from the rate") {
            AttitudeControl ac;

            ac.update(0.5F, 0.2F, std::nullopt);  // 0.5 rad/s for 0.2 s
            CHECK(ac.heading() == doctest::Approx(0.1F));

            ac.update(0.5F, 0.2F, std::nullopt);
            CHECK(ac.heading() == doctest::Approx(0.2F));
        }

        SUBCASE("the first mag fix snaps the estimate, later ones pull it") {
            AttitudeControl ac;

            // a long gyro-only drift, then the first absolute fix arrives
            ac.update(1.0F, 1.0F, std::nullopt);
            REQUIRE(ac.heading() == doctest::Approx(1.0F));
            ac.update(0.0F, 0.1F, 2.0F);
            CHECK(ac.heading() == doctest::Approx(2.0F));  // snapped, not blended

            // from then on the mag pulls with the filter's time constant - one sample moves the
            // estimate a fraction of the disagreement, never all of it
            ac.update(0.0F, 0.1F, 2.5F);
            CHECK(ac.heading() > 2.0F);
            CHECK(ac.heading() < 2.5F);
        }

        SUBCASE("with the mag steady, drift is pulled back out") {
            AttitudeControl ac;
            ac.update(0.0F, 0.1F, 1.0F);  // anchored at 1.0

            // a biased gyro walks the estimate while the mag keeps saying 1.0
            for (int i = 0; i < 200; i++) {
                ac.update(0.05F, 0.1F, 1.0F);  // a large fake bias
            }

            // the filter holds the estimate near the anchor instead of following the bias off
            // into the distance - the number is bias * tau, the filter's standing tradeoff
            CHECK(std::fabs(ac.heading() - 1.0F) < 0.2F);
        }

        SUBCASE("a commanded bearing survives leaving and re-entering POINTING") {
            AttitudeControl ac;
            ac.enter_pointing();
            ac.set_target(1.5F);

            // drift away, leave for a downlink pass, come back
            ac.update(0.5F, 1.0F, std::nullopt);
            ac.enter_pointing();

            // the ground named a bearing, and a mode round-trip must not quietly forget it -
            // re-capturing the current heading on entry is only for a vehicle never told to aim
            CHECK(ac.target() == doctest::Approx(1.5F));
        }

        SUBCASE("a large compass disagreement snaps the estimate back") {
            AttitudeControl ac;
            ac.update(0.0F, 0.1F, 1.0F);  // anchored at 1.0

            // the gyro lost the plot - a clipped flick left the integral 2 rad wrong. the next
            // fix must rescue the estimate outright, not drag it back at the filter's pace
            ac.update(20.0F, 0.1F, std::nullopt);  // integral runs off
            ac.update(0.0F, 0.1F, 1.0F);
            CHECK(ac.heading() == doctest::Approx(1.0F).epsilon(0.01));
        }

        SUBCASE("the error takes the short way round the circle") {
            AttitudeControl ac;
            ac.enter_pointing();

            // aim 216 degrees round - the exact case off the bench, where the raw subtraction
            // read -225.7 degrees of error and the controller drove the long way for it
            ac.set_target(3.78F);  // ~216.5 deg

            // the raw error is -3.78 rad (-216.5 deg); wrapped it is +2.50 rad (+143.5 deg),
            // the short way round. always inside a half turn, whatever was commanded
            CHECK(ac.heading_error() == doctest::Approx(6.28318F - 3.78F).epsilon(0.01));
            CHECK(std::fabs(ac.heading_error()) < 3.1415F);

            // positive error commands positive wheel torque, and the platform feels the negative
            // reaction - heading falls toward the target the short way rather than climbing 216
            // degrees to it
            const float cmd = ac.point(0.0F);
            CHECK(cmd > 0.0F);
        }

        SUBCASE("the reaction pushes back toward the target") {
            AttitudeControl ac;
            ac.enter_pointing();

            // drift a long way positive, well outside the band
            ac.update(1.0F, 1.0F, std::nullopt);
            const float cmd = ac.point(1.0F);

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

            const float cmd = ac.point(0.0F);  // zero error, zero rate

            REQUIRE(ac.pointing_in_band());
            CHECK(cmd == doctest::Approx(0.0F));
        }

        SUBCASE("outside the band it does correct") {
            AttitudeControl ac;
            ac.enter_pointing();
            REQUIRE(ac.pointing_in_band());

            // walk the heading past the band edge
            ac.update(ac.gains().pointing_band * 4.0F, 1.0F, std::nullopt);

            CHECK_FALSE(ac.pointing_in_band());
            CHECK(std::fabs(ac.point(0.0F)) > 0.0F);
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
