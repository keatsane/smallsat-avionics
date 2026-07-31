/**
 * @file   attitude_control.cpp
 * @brief  see attitude_control.hpp for why the law is this small
 */

#include "fsw/attitude_control.hpp"

#include <cmath>

namespace fsw {

float AttitudeControl::limit(float torque) const {
    if (torque > g_.torque_max) {
        saturated_ = true;
        return g_.torque_max;
    }
    if (torque < -g_.torque_max) {
        saturated_ = true;
        return -g_.torque_max;
    }
    saturated_ = false;
    return torque;
}

float AttitudeControl::detumble(float rate_rads) const {
    // inside the deadband the vehicle is as detumbled as it can be measured to be, so stop
    // pushing. this is not a rounding convenience - a controller that keeps answering a rate it
    // cannot resolve spends wheel momentum on noise, and the wheel is the one resource here that
    // does not come back
    if (std::fabs(rate_rads) < g_.rate_deadband) {
        saturated_ = false;
        return 0.0F;
    }

    // oppose the rate, proportionally - but note the sign follows the rate rather than opposing
    // it, because what is commanded here is the *wheel's* torque and the platform feels the
    // reaction, equal and opposite. spinning the wheel the same way the platform is turning is
    // what pushes the platform back.
    //
    // this was backwards on the first closed-loop run and the sim caught it immediately: the
    // controller removed 0.011 rad/s in a cycle where friction alone had been removing 0.57, so
    // it was fighting the thing already stopping the platform. worth knowing that a plain
    // open-loop unit test cannot find this - only running it against physics can
    return limit(g_.k_rate * rate_rads);
}

namespace {

// past this the gyro integral is not being corrected, it is being rescued - see update()
constexpr float kMagSnapRad = 1.0F;  // ~57 deg

// wrap any angle into [-pi, pi]
float wrap_pi(float rad) {
    constexpr float kTwoPi = 6.28318530718F;
    while (rad > kTwoPi / 2.0F) {
        rad -= kTwoPi;
    }
    while (rad < -kTwoPi / 2.0F) {
        rad += kTwoPi;
    }
    return rad;
}

}  // namespace

void AttitudeControl::update(float rate_rads, float dt_s, const std::optional<float>& mag_heading) {
    heading_ += rate_rads * dt_s;

    if (mag_heading) {
        const float err = wrap_pi(*mag_heading - heading_);
        if (!have_mag_ || std::fabs(err) > kMagSnapRad) {
            // snap rather than blend: on the first fix because converging from an arbitrary zero
            // spends seconds being deliberately wrong, and on a large disagreement because that
            // means the gyro lost the plot (a clipped flick, mostly) - the compass is the one
            // that knows, and dragging back a 150-degree error at the filter's pace looks
            // exactly like "stuck on the wrong heading" from the bench
            heading_ += err;
            have_mag_ = true;
        } else if (dt_s > 0.0F) {
            const float alpha = dt_s / (g_.mag_tau_s + dt_s);
            heading_ += alpha * err;
        }
    }
}

float AttitudeControl::heading() const { return wrap_pi(heading_); }

void AttitudeControl::enter_pointing() {
    if (!target_commanded_) {
        target_ = heading_;  // never told where to aim - hold here
    }
    saturated_ = false;
}

// the error the controller acts on, wrapped to the short way round. without this a target of
// 216 degrees from a heading of -9 read as an error of -225 - the controller drove the long way,
// through 226 degrees of travel, when +134 the other way reaches the same bearing. a circle has
// two ways round and the raw subtraction always picks whichever side of zero the arithmetic
// landed on, not the shorter
float AttitudeControl::wrapped_error() const { return wrap_pi(heading_ - target_); }

bool AttitudeControl::pointing_in_band() const {
    return std::fabs(wrapped_error()) < g_.pointing_band;
}

float AttitudeControl::point(float rate_rads) {
    // inside the band, damp the rate but stop chasing the angle. correcting an error smaller than
    // the band asks for a torque under the bearing's breakaway, the platform does not move, and
    // the wheel spends momentum on a correction that was never going to arrive
    if (pointing_in_band()) {
        return limit(g_.k_damp * rate_rads);
    }

    // PD. same sign convention as detumble - the wheel is commanded and the platform feels the
    // reaction, so both terms follow their error rather than opposing it
    return limit((g_.k_angle * wrapped_error()) + (g_.k_damp * rate_rads));
}

}  // namespace fsw
