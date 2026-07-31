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

void AttitudeControl::enter_pointing() {
    heading_err_ = 0.0F;
    saturated_ = false;
}

bool AttitudeControl::pointing_in_band() const {
    return std::fabs(heading_err_) < g_.pointing_band;
}

float AttitudeControl::point(float rate_rads, float dt_s) {
    // heading comes from integrating the rate, because nothing on this vehicle measures it. that
    // means it drifts, and the drift is why this holds an attitude rather than pointing at one
    heading_err_ += rate_rads * dt_s;

    // inside the band, damp the rate but stop chasing the angle. correcting an error smaller than
    // the band asks for a torque under the bearing's breakaway, the platform does not move, and
    // the wheel spends momentum on a correction that was never going to arrive
    if (pointing_in_band()) {
        return limit(g_.k_damp * rate_rads);
    }

    // PD. same sign convention as detumble - the wheel is commanded and the platform feels the
    // reaction, so both terms follow their error rather than opposing it
    return limit((g_.k_angle * heading_err_) + (g_.k_damp * rate_rads));
}

}  // namespace fsw
