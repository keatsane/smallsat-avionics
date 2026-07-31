#include <cmath>

#include "doctest.h"
#include "plant.hpp"

using namespace fsw::sil;

// the plant is test scaffolding, so these are not requirement cases - they are the checks that
// keep the scaffolding honest. a model that quietly breaks conservation, or lets friction add
// energy, will happily "prove" a control law that cannot work on the bench - and the failure
// turns up as hardware behaving nothing like the sim

namespace {

// friction off - for the conservation laws, which only hold on an isolated system
PlantParams frictionless() {
    PlantParams p;
    p.friction_viscous = 0.0;
    p.friction_static = 0.0;
    p.friction_kinetic = 0.0;
    return p;
}

}  // namespace

TEST_SUITE("PLANT MODEL") {
    TEST_CASE("momentum is exchanged, not created") {
        SUBCASE("total angular momentum is conserved with no friction") {
            Plant plant(frictionless());
            const double before = plant.momentum();
            REQUIRE(before == doctest::Approx(0.0));

            for (int i = 0; i < 50; ++i) {
                plant.step(0.01, 0.1);  // 5 s of steady torque
            }

            // the wheel spun up and the platform went the other way, and the two cancel
            CHECK(plant.wheel_rate() > 0.0);
            CHECK(plant.platform_rate() < 0.0);
            CHECK(plant.momentum() == doctest::Approx(0.0).epsilon(1e-9));
        }

        SUBCASE("the platform turns the opposite way to the wheel") {
            Plant plant(frictionless());
            plant.step(-0.01, 1.0);

            CHECK(plant.wheel_rate() < 0.0);
            CHECK(plant.platform_rate() > 0.0);
        }

        SUBCASE("a constant wheel speed exerts no torque") {
            // the rig's demo is a transient, and this is why: reaction exists while the wheel's
            // speed is changing, not while it is high
            Plant plant(frictionless());
            plant.step(0.01, 1.0);
            const double spun = plant.platform_rate();

            plant.step(0.0, 5.0);  // wheel keeps spinning, nothing commanded

            CHECK(plant.wheel_rate() > 0.0);                        // still spinning
            CHECK(plant.platform_rate() == doctest::Approx(spun));  // platform unchanged
        }
    }

    TEST_CASE("stiction") {
        SUBCASE("a torque under breakaway moves nothing at all") {
            Plant plant;            // default params: breakaway 0.02 N m
            plant.step(0.01, 2.0);  // reaction 0.01, under it

            CHECK(plant.platform_rate() == doctest::Approx(0.0));
            CHECK(plant.platform_angle() == doctest::Approx(0.0));
            CHECK(plant.wheel_rate() > 0.0);  // the wheel still spun up - it is not stuck
        }

        SUBCASE("a torque over breakaway does move it") {
            Plant plant;
            // 0.1 s, not 1 s: at full torque this wheel saturates in about 0.18 s, after which
            // no reaction reaches the platform and friction brings it straight back to rest.
            // stepping a whole second here reads zero and looks like stiction never broke
            plant.step(0.05, 0.1);

            CHECK(std::fabs(plant.platform_rate()) > 0.0);
        }

        SUBCASE("a moving platform comes to a stop and stays stopped") {
            Plant plant;
            plant.set_platform_rate(1.0);

            plant.step(0.0, 10.0);  // no command - friction alone

            CHECK(plant.platform_rate() == doctest::Approx(0.0));

            // and does not creep afterwards. without the zero-crossing guard the sign flips every
            // substep here and the rate chatters instead of settling
            const double resting = plant.platform_angle();
            plant.step(0.0, 5.0);
            CHECK(plant.platform_angle() == doctest::Approx(resting));
        }

        SUBCASE("friction only ever removes energy") {
            Plant plant;
            plant.set_platform_rate(2.0);
            double last = std::fabs(plant.platform_rate());

            for (int i = 0; i < 100; ++i) {
                plant.step(0.0, 0.05);
                const double now = std::fabs(plant.platform_rate());
                CHECK(now <= last + 1e-12);  // monotonically slowing, never a speed-up
                last = now;
            }
        }
    }

    TEST_CASE("actuator limits") {
        SUBCASE("torque is clamped to what the motor can deliver") {
            Plant a;
            Plant b;
            a.step(a.params().torque_max, 1.0);
            b.step(a.params().torque_max * 100.0, 1.0);  // ask for absurdly more

            CHECK(a.wheel_rate() == doctest::Approx(b.wheel_rate()));
        }

        SUBCASE("a saturated wheel stops accepting momentum") {
            Plant plant(frictionless());
            for (int i = 0; i < 2000; ++i) {
                plant.step(plant.params().torque_max, 0.1);
            }

            REQUIRE(plant.wheel_saturated());
            CHECK(plant.wheel_rate() ==
                  doctest::Approx(plant.params().wheel_rate_max).epsilon(0.01));

            // and the platform stops being pushed, which is what losing control authority means
            const double stalled = plant.platform_rate();
            plant.step(plant.params().torque_max, 1.0);
            CHECK(plant.platform_rate() == doctest::Approx(stalled));
        }

        SUBCASE("a saturated wheel can still be slowed") {
            // the only way authority comes back on a bench with no momentum dumping
            Plant plant(frictionless());
            for (int i = 0; i < 2000; ++i) {
                plant.step(plant.params().torque_max, 0.1);
            }
            REQUIRE(plant.wheel_saturated());

            // briefly - a full second of reverse torque does not desaturate the wheel, it drives
            // it to the opposite limit and it reads saturated again
            plant.step(-plant.params().torque_max, 0.05);

            CHECK(plant.wheel_rate() < plant.params().wheel_rate_max);
            CHECK_FALSE(plant.wheel_saturated());
        }

        SUBCASE("the wheel saturates in well under one control cycle at full torque") {
            // the number the control law has to be designed around. j_wheel * rate_max /
            // torque_max is ~0.18 s, so a full-torque command is spent inside two control cycles
            // and everything after it is a wheel that can only be unwound. a rate controller that
            // asks for max torque will saturate before it has seen its own effect
            Plant plant(frictionless());
            const auto& p = plant.params();
            const double expected_s = (p.j_wheel * p.wheel_rate_max) / p.torque_max;
            CHECK(expected_s < 0.25);

            double t = 0.0;
            while (!plant.wheel_saturated() && t < 5.0) {
                plant.step(p.torque_max, 0.001);
                t += 0.001;
            }

            CHECK(plant.wheel_saturated());
            CHECK(t == doctest::Approx(expected_s).epsilon(0.02));
        }
    }

    TEST_CASE("integration is stable across step sizes") {
        // the shim will call this once per 100 ms control cycle; the substepping is what makes
        // that give the same answer as calling it far more often
        Plant coarse(frictionless());
        Plant fine(frictionless());

        for (int i = 0; i < 10; ++i) {
            coarse.step(0.02, 0.1);
        }
        for (int i = 0; i < 1000; ++i) {
            fine.step(0.02, 0.001);
        }

        CHECK(coarse.platform_rate() == doctest::Approx(fine.platform_rate()).epsilon(1e-6));
        CHECK(coarse.wheel_rate() == doctest::Approx(fine.wheel_rate()).epsilon(1e-6));
    }
}
