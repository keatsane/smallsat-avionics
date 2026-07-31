/**
 * @file   plant.cpp
 * @brief  the integrator - see plant.hpp for what is modelled and what deliberately is not
 */

#include "plant.hpp"

#include <algorithm>
#include <cmath>

namespace fsw::sil {

namespace {

// fixed internal substep. friction switches state discontinuously at the stick/slip boundary, and
// a step large enough to cross that boundary in one go integrates through a discontinuity and
// invents energy. 1 ms is ~100 substeps per control cycle, which is cheap on a host
constexpr double kSubstepS = 1.0e-3;

double sign_of(double x) {
    if (x > 0.0) {
        return 1.0;
    }
    return (x < 0.0) ? -1.0 : 0.0;
}

}  // namespace

bool Plant::wheel_saturated() const { return std::fabs(w_wheel_) >= p_.wheel_rate_max; }

double Plant::deliverable(double torque_nm) const {
    // the actuator's own ceiling first
    double t = std::clamp(torque_nm, -p_.torque_max, p_.torque_max);

    // then saturation: at the wheel's top speed a torque that would spin it faster buys nothing.
    // note the direction test - a saturated wheel can still be *slowed*, which is the only way
    // control authority ever comes back on a bench with no momentum dumping
    if (w_wheel_ >= p_.wheel_rate_max && t > 0.0) {
        t = 0.0;
    } else if (w_wheel_ <= -p_.wheel_rate_max && t < 0.0) {
        t = 0.0;
    }
    return t;
}

void Plant::substep(double torque_nm, double dt_s) {
    const double tau = deliverable(torque_nm);

    // the wheel takes the commanded torque directly
    w_wheel_ += (tau / p_.j_wheel) * dt_s;

    // and the platform takes the reaction - equal and opposite, which is the whole mechanism
    const double tau_react = -tau;

    const bool at_rest = std::fabs(w_platform_) < p_.rest_rate;

    if (at_rest) {
        // stuck: nothing happens at all until the reaction clears breakaway. this is the branch
        // that makes a pointing controller hunt - a small angle error asks for a small torque,
        // the platform does not move, and the error never closes
        if (std::fabs(tau_react) <= p_.friction_static) {
            w_platform_ = 0.0;
            return;
        }
        // just broke loose. friction drops from static to kinetic the moment it does, so the
        // platform lurches rather than easing off
        const double tau_net = tau_react - (sign_of(tau_react) * p_.friction_kinetic);
        w_platform_ += (tau_net / p_.j_platform) * dt_s;
    } else {
        const double tau_fric =
            -(sign_of(w_platform_) * p_.friction_kinetic) - (p_.friction_viscous * w_platform_);
        const double tau_net = tau_react + tau_fric;
        const double w_next = w_platform_ + ((tau_net / p_.j_platform) * dt_s);

        // friction decelerates, it does not reverse. if the step carried the rate through zero,
        // the platform actually came to rest inside this step - without this guard the sign flips
        // every substep and the model chatters at the substep frequency instead of stopping
        if (sign_of(w_next) != sign_of(w_platform_) && std::fabs(tau_react) <= p_.friction_static) {
            w_platform_ = 0.0;
        } else {
            w_platform_ = w_next;
        }
    }

    angle_ += w_platform_ * dt_s;
}

void Plant::step(double torque_nm, double dt_s) {
    if (dt_s <= 0.0) {
        return;
    }

    // the command is held across the whole interval, which is what the esc does between cycles
    double remaining = dt_s;
    while (remaining > 0.0) {
        const double h = std::min(kSubstepS, remaining);
        substep(torque_nm, h);
        remaining -= h;
    }
}

}  // namespace fsw::sil
