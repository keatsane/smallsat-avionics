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
#include <optional>

namespace fsw {

/**
 * @brief  tuning for the single-axis law
 *
 * Gains are starting points from the plant model's estimated parameters, not measurements. They
 * are expected to change once the rig can be commanded (REQ-ADCS-001).
 */
struct ControlGains {
    // detumble: torque per unit body rate. the law is tau_wheel = +k_rate * omega - the platform
    // feels the reaction, so a wheel spun the way the platform turns is what slows it. bleeds rate
    // away without ever needing to know the platform's inertia. see detumble() for why the sign
    // reads backwards at a glance
    //
    // retuned 0.02 -> 0.008 on the rig (2026-08-03). at 0.02 any rate past ~0.8 rad/s commanded
    // more torque than the bearing's breakaway (~0.016 N m), so the loop kept kicking the platform
    // loose in alternating directions instead of letting it settle - a stick-slip limit cycle at
    // roughly 90 deg/s that the model never showed, because the model's breakaway is a guess
    float k_rate = 0.008F;  // N m per rad/s

    // what the actuator is allowed to be asked for. the plant clamps to what the motor can
    // deliver anyway; asking inside that limit is what keeps the request honest (REQ-ADCS-003)
    float torque_max = 0.05F;  // N m

    // below this the platform counts as detumbled and the law stops pushing. without a deadband
    // the controller chases sensor noise forever and the wheel walks itself into saturation
    // holding a rate nobody can measure
    float rate_deadband = 0.02F;  // rad/s

    // pointing, outer loop: how much slew rate one radian of heading error asks for. this used to
    // be a torque per radian (k_angle = 0.4), which made POINTING a relay rather than a
    // controller - 7 degrees of error already demanded the wheel's whole ceiling, so every
    // manoeuvre past a few degrees ran at full torque until it overshot. the rig answered with
    // 130 deg/s slams and 19 degrees of overshoot (2026-08-04). asking for a *rate* instead keeps
    // the request inside what the wheel can actually deliver
    float k_slew = 0.6F;  // rad/s per rad of error

    // the fastest slew POINTING will ask for. the wheel's entire momentum budget is ~0.87 rad/s
    // of platform rate, so anything approaching that spends the whole budget on the manoeuvre and
    // leaves nothing to stop with. a third of it is a request the actuator can still answer
    float slew_rate_max = 0.3F;  // rad/s, ~17 deg/s

    // pointing, inner loop: torque per rad/s of rate error. this is detumble's law with a moving
    // setpoint, so it MUST carry detumble's gain - the plant does not care which mode is asking.
    // 0.10 was tried on the rig to give the slew enough torque to break the bearing loose, and it
    // oscillated at +/-215 deg/s within one command (2026-08-04); the stable value is the one
    // measured for detumble, and the two are now pinned together deliberately.
    //
    // the consequence is the honest finding of this rig: at a stable gain a full rate request
    // commands ~2 mN m, and the bearing does not break loose until ~16. POINTING can therefore
    // hold an attitude and damp disturbances, but it cannot slew to a new bearing. that gap is
    // mechanical - the lazy susan's stiction against the wheel's torque - not a gain to be found
    float k_damp = 0.008F;  // N m per rad/s, tied to k_rate

    // stiction feedforward: a fixed push in the direction of the error, applied only while the
    // platform is not moving. friction is the one disturbance a feedback gain cannot answer on
    // this rig - a gain large enough to break the bearing loose is a gain large enough to
    // oscillate, which the bench showed twice - so the authority comes from a constant instead. a
    // constant adds no loop gain at all, so it cannot destabilise anything.
    //
    // 8 mN m: the platform first answered a pulse at ~6 mN m on the bench (2026-08-04, measured
    // with the wheel itself), so this clears it with a little margin. that measurement had a USB
    // cable on the platform acting as a return spring, so it is a bound rather than a clean
    // number - worth repeating untethered
    float stiction_ff = 0.008F;  // N m

    // below this the platform counts as stopped and the feedforward is allowed. once it is
    // sliding the bearing needs less, and a push that stayed on would keep accelerating it
    float stuck_rate_rads = 0.05F;  // rad/s, ~3 deg/s

    // the error band REQ-ADCS-002 is held to, and the band the law stops correcting inside.
    // ~2.9 degrees, which is about what a printed rig on a lazy susan can be asked for
    float pointing_band = 0.05F;  // rad

    // the rate above which the executive pulls the vehicle into DETUMBLE on its own
    // (REQ-MODE-012). well clear of sensor noise and of the wobble a capture or a touch leaves
    // behind, and well under a real spin - ~20 deg/s
    float detumble_enter_rads = 0.35F;  // rad/s

    // how quickly the heading estimate is pulled toward the magnetometer. the gyro integral is
    // smooth and drifts; the mag is absolute and jittery (indoor fields bend around every steel
    // shelf) - the classic complementary split, gyro for shape, mag for anchor. two seconds
    // trusts the gyro over any single mag sample while still killing drift within a few
    float mag_tau_s = 2.0F;
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
     * @brief  advance the heading estimate - call every cycle, in every mode
     * @param  rate_rads    measured yaw rate
     * @param  dt_s         seconds since the last call
     * @param  mag_heading  absolute heading off the magnetometer, when a valid sample exists
     *
     * A complementary filter: the gyro integral gives the estimate its shape, the mag pulls it
     * toward truth with the time constant in the gains. The first mag sample snaps the estimate
     * outright - converging from an arbitrary zero would spend seconds being wrong on purpose.
     * Estimation runs in every mode, which is what makes the heading survive mode changes and
     * the ground display track the platform even in STANDBY.
     */
    void update(float rate_rads, float dt_s, const std::optional<float>& mag_heading);

    /**
     * @brief  arm the pointing target on entry to POINTING
     *
     * If the ground has ever named a bearing (SET_HEADING), that bearing stands - a commanded
     * aim outlives mode changes, so a downlink round-trip returns to the same target. Only a
     * vehicle that was never told where to point holds wherever it happens to be.
     */
    void enter_pointing();

    /**
     * @brief  torque to drive the heading to the target (REQ-ADCS-002)
     * @param  rate_rads  measured yaw rate
     * @return commanded wheel torque in N m, already saturation-limited
     *
     * Proportional on the wrapped heading error, derivative on rate. update() owns the estimate;
     * this only acts on it.
     */
    float point(float rate_rads);

    /**
     * @brief  aim at an absolute bearing
     * @param  rad  target heading, in the magnetometer's calibrated frame
     *
     * Zero is wherever the platform's camera face pointed when the mounting offset was
     * calibrated (kMagMountOffsetRad on the target). With no mag sample ever received the
     * estimate free-runs from zero-at-boot, and the target is relative to that - degraded but
     * usable, and honest about which reference actually exists (REQ-ADCS-002).
     */
    void set_target(float rad) {
        target_ = rad;
        target_commanded_ = true;  // and it stays commanded - see enter_pointing
    }

    /** @brief the bearing being held */
    float target() const { return target_; }

    /** @brief the heading estimate, wrapped to [-pi, pi] */
    float heading() const;

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

    // the heading estimate, unbounded - winding is information the wrap for telemetry discards
    float heading_ = 0.0F;
    bool have_mag_ = false;  // stays false until the first mag sample anchors the estimate
    mutable bool saturated_ = false;

    // the bearing to hold, in the heading estimate's frame
    float target_ = 0.0F;

    // whether the ground has ever named a bearing. a commanded target outlives mode changes -
    // leaving POINTING for a downlink pass and coming back must not quietly forget where the
    // ground said to aim, which is exactly what re-capturing the current heading on entry did
    bool target_commanded_ = false;
};

}  // namespace fsw

#endif  // FSW_ATTITUDE_CONTROL_HPP
