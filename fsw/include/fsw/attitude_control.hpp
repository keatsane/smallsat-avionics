/**
 * @file   attitude_control.hpp
 * @brief  single-axis attitude control - the law that turns a measured rate into a wheel torque
 *
 * Deliberately simple, and the simplicity is a decision rather than a stage. The bench rig's
 * dominant behaviour is stiction in a cheap bearing, and the plant's parameters are estimates, so
 * a law that inverts the model would be inverting numbers nobody has measured. Proportional
 * feedback needs none of them - it needs only the sign of the effect to be right, and gets its
 * final gains from the rig.
 *
 * No integral term. Integral exists to erase steady-state error, and against stiction it does the
 * opposite: it winds up while the platform is stuck, then breaks loose and overshoots. Add one
 * only with anti-windup, and only after the bench shows a steady-state error worth erasing.
 */

#ifndef FSW_ATTITUDE_CONTROL_HPP
#define FSW_ATTITUDE_CONTROL_HPP

#include <cstdint>

namespace fsw {

/**
 * @brief  tuning for the single-axis law
 *
 * Gains are starting points from the plant model's estimated parameters, not measurements. They
 * are expected to change once the rig can be commanded (REQ-ADCS-001).
 */
struct ControlGains {
    // detumble: torque per unit body rate. the whole law is tau = -k_rate * omega, which bleeds
    // rate away without ever needing to know the platform's inertia
    float k_rate = 0.02F;  // N m per rad/s

    // what the actuator is allowed to be asked for. the plant clamps to what the motor can
    // deliver anyway; asking inside that limit is what keeps the request honest (REQ-ADCS-003)
    float torque_max = 0.05F;  // N m

    // below this the platform counts as detumbled and the law stops pushing. without a deadband
    // the controller chases sensor noise forever and the wheel walks itself into saturation
    // holding a rate nobody can measure
    float rate_deadband = 0.02F;  // rad/s

    // pointing, proportional: torque per radian of heading error. sized so a small error still
    // clears the bearing's breakaway torque - under it the platform simply does not move and the
    // error never closes, which is the stiction trap this rig will actually hit
    float k_angle = 0.4F;  // N m per rad

    // pointing, derivative: torque per rad/s. this is the damping - without it the platform
    // overshoots the target and oscillates around it rather than settling
    float k_damp = 0.06F;  // N m per rad/s

    // the error band REQ-ADCS-002 is held to, and the band the law stops correcting inside.
    // ~2.9 degrees, which is about what a printed rig on a lazy susan can be asked for
    float pointing_band = 0.05F;  // rad
};

/**
 * @brief  the single-axis controller
 *
 * Stateless apart from its saturation bookkeeping, so it is a pure function of the rate it is
 * given - which is what keeps it identical in SIL, on the host, and on the vehicle.
 */
class AttitudeControl {
   public:
    AttitudeControl() = default;
    explicit AttitudeControl(const ControlGains& g) : g_(g) {}

    /**
     * @brief  torque to null the body rate (REQ-ADCS-001)
     * @param  rate_rads  measured yaw rate
     * @return commanded wheel torque in N m, already saturation-limited
     */
    float detumble(float rate_rads) const;

    /**
     * @brief  take the current heading as the target to hold
     *
     * Called on entry to POINTING. There is no absolute heading reference on this vehicle - the
     * gyro measures rate and the magnetometer sits next to a motor full of magnets - so "hold a
     * commanded attitude" means holding wherever the ground pointed it when it commanded the
     * mode. Zeroing the accumulated error is the whole of it.
     */
    void enter_pointing();

    /**
     * @brief  torque to hold the captured heading (REQ-ADCS-002)
     * @param  rate_rads  measured yaw rate
     * @param  dt_s       seconds since the last call, for integrating rate into heading
     * @return commanded wheel torque in N m, already saturation-limited
     *
     * Proportional on heading error, derivative on rate. The heading comes from integrating the
     * rate here rather than from a sensor, so it drifts - acceptable for holding an attitude over
     * a demo, and the reason this is attitude *hold* rather than absolute pointing.
     */
    float point(float rate_rads, float dt_s);

    /**
     * @brief  aim at a bearing relative to where POINTING was entered
     * @param  rad  target, measured from the entry heading
     *
     * Relative because there is nothing absolute to measure against: the gyro gives rate and the
     * magnetometer sits beside a motor full of magnets. So a commanded heading means "this far
     * round from where you were when the mode started", which is honest about what the vehicle
     * can actually know (REQ-ADCS-002).
     */
    void set_target(float rad) { target_ = rad; }

    /** @brief the bearing being held, relative to the entry heading */
    float target() const { return target_; }

    /** @brief heading relative to where POINTING was entered, in radians */
    float heading() const { return heading_err_; }

    /** @brief how far the vehicle is from the bearing it was told to hold, the short way round */
    float heading_error() const { return wrapped_error(); }

    /** @brief true while the heading error is inside the band REQ-ADCS-002 asks for */
    bool pointing_in_band() const;

    /** @brief heading error wrapped to [-pi, pi] - a circle has two ways round, take the shorter */
    float wrapped_error() const;

    /**
     * @brief  did the last command hit the actuator limit
     * Sustained saturation is what REQ-ADCS-003 raises a fault on, so the executive needs to see
     * it rather than infer it from the torque it just asked for.
     */
    bool saturated() const { return saturated_; }

    const ControlGains& gains() const { return g_; }

   private:
    float limit(float torque) const;

    ControlGains g_{};
    mutable bool saturated_ = false;

    // heading relative to wherever POINTING was entered, integrated from the rate
    float heading_err_ = 0.0F;

    // the bearing to hold, in the same frame. zero until the ground says otherwise, which makes
    // an uncommanded POINTING exactly the attitude hold it was before this existed
    float target_ = 0.0F;
};

}  // namespace fsw

#endif  // FSW_ATTITUDE_CONTROL_HPP
