/**
 * @file   plant.hpp
 * @brief  single-axis rigid body with one reaction wheel - the bench rig's physics, for SIL
 *
 * This is test scaffolding, not flight software, and it lives outside fsw/src on purpose: nothing
 * here may ever link into the STM32 image. The PAL boundary check (tools/tests/test_pal_boundary)
 * scans fsw/src and fsw/include only, and that is the reason this file is not in either.
 *
 * What it models, and why only this much:
 *
 *   1. Momentum exchange. The motor pushes the wheel, the wheel pushes back on the platform. That
 *      is the whole trick of a reaction wheel and it is the thing the control law is written
 *      against. Reaction torque exists only while the wheel's speed is *changing* - a steadily
 *      spinning wheel does nothing, which is why the rig's demo is a transient.
 *   2. Friction, viscous plus stiction. The platform sits on a cheap lazy-susan bearing and
 *      **stiction is what will dominate its behaviour** - below a breakaway torque it does not
 *      move at all, then it breaks loose all at once. This is the effect that decided against
 *      NASA 42: 42 models a superb space environment and none of this.
 *   3. Wheel saturation. The wheel has a top speed; past it there is no control authority left.
 *      A real spacecraft dumps momentum with magnetorquers, the bench cannot, so the control law
 *      has to respect the limit rather than expect to escape it (REQ-ADCS-003).
 *
 * Deliberately absent: motor electrical dynamics. The ESC closes its current loop at ~20 kHz
 * against a 10 Hz control cycle, so from the platform's point of view the commanded torque is
 * granted instantly. Modelling it would add state that settles a thousand times faster than
 * anything it feeds.
 */

#ifndef FSW_SIL_PLANT_HPP
#define FSW_SIL_PLANT_HPP

namespace fsw::sil {

/**
 * @brief  the rig's physical constants
 *
 * The two inertias are MEASURED - read out of the Fusion assembly on 2026-07-31 as the Izz term
 * of the moment-of-inertia tensor about the spin axis, converted from g mm^2 (Fusion's unit) by
 * 1e-9. Everything else is still an estimate.
 *
 * Fusion integrates the real geometry, so those two are better than a scale could give: a scale
 * reports mass and leaves the radius distribution to guesswork, and inertia is mass times radius
 * squared. Their weak point is what the CAD is missing - connectors, wiring, unmodelled boards -
 * which sit near the walls and would push j_platform up, so it reads slightly low.
 *
 * friction_static is now the one number that matters and is not measured. hardware.md has the
 * procedure: hang a known weight on a string at a known radius and find the force that just
 * starts the platform turning; torque = force x radius.
 */
struct PlantParams {
    // rotating assembly about the yaw axis, less the wheel: the whole stack's Izz (4.639e-3)
    // minus the flywheel's own (1.09e-4), because the wheel is the other body in this model
    double j_platform = 4.53e-3;  // kg m^2, measured

    // the flywheel about its own axis, AS BUILT - 8 of its 16 weight pockets filled with M4 steel
    // bolts and nuts. bare PLA is 7.63e-5 and the 8 bolts add 3.27e-5, so filling the other eight
    // would reach ~1.42e-4 and buy ~30% more momentum capacity. that is the cheapest authority on
    // the rig, and hardware.md's "fill outermost first" is why it is worth doing
    double j_wheel = 1.09e-4;  // kg m^2, measured

    // what the GBM4108 can actually deliver through the ESC. anything the control law asks for
    // beyond this is simply not granted, which is what makes saturation a real behaviour
    double torque_max = 0.06;  // N m

    // wheel top speed. the rig measured ~344 rpm on the bench (2026-07-21), so ~36 rad/s.
    //
    // this and j_wheel together are the vehicle's whole momentum budget: 1.09e-4 * 36 = 0.0039
    // kg m^2/s, which against j_platform is only **0.87 rad/s (50 deg/s) of platform rate** the
    // wheel can absorb before it is full. that is four times less than the estimated parameters
    // suggested, and it is the hard ceiling every manoeuvre on this rig has to fit inside
    double wheel_rate_max = 36.0;  // rad/s

    // the three friction terms are fitted to one coast-down measurement (2026-07-31): spun by
    // hand to roughly one revolution per second and timed to a stop, ~6.28 rad/s to rest in
    // ~2 s. that is a deceleration of ~3.1 rad/s^2, so ~0.014 N m of drag against j_platform.
    //
    // one measurement cannot separate coulomb from viscous - both produce a decay and only the
    // *shape* distinguishes them, which a hand-timed stopwatch cannot resolve. the split below
    // assumes coulomb-dominant, which is the right prior for a loaded ball bearing, and puts
    // just enough viscous in to keep the term honest. re-fit if the decay is ever logged rather
    // than eyeballed - the gyro can do it once the vehicle can be spun and left alone.
    //
    // it is a rough measurement and it still mattered: the model was ~30% too draggy before it

    // UPDATE 2026-08-04: fitted to two decays rather than one, which is what separates coulomb
    // from viscous. Both measure the platform coasting with no torque commanded, so neither
    // involves the motor and neither can be contaminated by it:
    //
    //   mean 3.14 rad/s, 3.14 rad/s^2  =>  14.2 mN m of drag   (hand coast-down, 2026-07-31)
    //   mean 0.78 rad/s, 2.82 rad/s^2  =>  12.8 mN m of drag   (detumble A/B, the SAFE half,
    //                                                           logged at 10 Hz on the wired link)
    //
    // Two points on drag(w) = coulomb + viscous*w give coulomb ~12.3 mN m, viscous ~0.6 mN m per
    // rad/s - which is within a few percent of what this model already had from a single
    // measurement and a prior. The original fit was good.
    //
    // It took two wrong revisions the same day to establish that, and both are worth remembering.
    // The first read the breakaway sweep's 5 mN m as bearing stiction and pulled coulomb down to
    // 4.0 - but the wheel does not rotate below ~5 mN m commanded either, so that number is at
    // least partly the motor's cogging (see friction_static). The second read the A/B's decay as
    // 120 deg/s^2 by averaging a stretch where the platform was still being pushed, and landed on
    // 8.0. The arrest alone - the last unbroken run of falling rate - is 161 deg/s^2, and that is
    // the number above.
    //
    // What caught it was tools/overlay.py: the model and the rig were run through the same coast
    // and disagreed by 1.5x, which is not something a model fitted to that very run can do. A
    // plant model checked only against itself will absorb a bad measurement without complaint.

    // viscous drag - proportional to speed, and the well-behaved part
    double friction_viscous = 5.9e-4;  // N m per rad/s

    // once sliding, friction drops. that drop is what makes stiction lurch rather than ease
    double friction_kinetic = 12.3e-3;  // N m

    // breakaway: the platform does not move until applied torque exceeds this. still inferred,
    // and the reason is worth keeping, because it looked measured for about an hour.
    //
    // the PULSE_WHEEL sweep found the platform first moving at 5 mN m *commanded*, and that was
    // briefly written in here as the static term. Then the wheel was watched on its own: at 3 and
    // 4 mN m the rotor turns ~3 degrees, holds while the torque is applied, and springs straight
    // back when it stops. That is cogging, not rotation - and a wheel that does not rotate puts
    // no reaction on the platform at all, because the motor torque and the cogging that opposes
    // it are both internal. So the sweep found whichever threshold binds first, the bearing's
    // stiction or the motor's detent, and cannot say which.
    //
    // 1.25x kinetic is the usual ratio for a bearing under load, and it is still a prior rather
    // than a measurement. What the sweep does bound is the *commanded* torque that moves this
    // platform - 5 mN m - and against a static friction of ~15 mN m that implies the commanded
    // scale runs roughly 3x optimistic. kOhmsPerKt in platform_stm32.cpp carries the same
    // conclusion from a different direction; neither is precise enough to correct it on yet.
    double friction_static = 15.4e-3;  // N m

    // below this the platform counts as stopped. a threshold rather than an exact zero, because
    // floating point never lands on zero and a sign test on a number that never settles chatters
    double rest_rate = 1.0e-4;  // rad/s
};

/**
 * @brief  single-axis platform plus reaction wheel, integrated forward in time
 */
class Plant {
   public:
    Plant() = default;
    explicit Plant(const PlantParams& p) : p_(p) {}

    /**
     * @brief  advance the physics by one interval
     * @param  torque_nm  what the control law asked the wheel for, in N m
     * @param  dt_s       how far to advance, in seconds
     *
     * Substeps internally, so a caller stepping a whole 100 ms control cycle at once still gets a
     * stable answer. The commanded torque is held constant across the interval, which is exactly
     * what the real ESC does between cycles.
     */
    void step(double torque_nm, double dt_s);

    /** @brief set the platform's initial spin, for a detumble scenario */
    void set_platform_rate(double rate_rad_s) { w_platform_ = rate_rad_s; }

    /** @brief platform heading in radians, accumulated (not wrapped - winding is information) */
    double platform_angle() const { return angle_; }

    /** @brief platform yaw rate in rad/s - this is what the gyro would read */
    double platform_rate() const { return w_platform_; }

    /** @brief wheel speed in rad/s */
    double wheel_rate() const { return w_wheel_; }

    /** @brief true when the wheel is at its limit and cannot absorb more momentum */
    bool wheel_saturated() const;

    /** @brief total angular momentum, platform plus wheel - conserved when friction is off */
    double momentum() const { return (p_.j_platform * w_platform_) + (p_.j_wheel * w_wheel_); }

    const PlantParams& params() const { return p_; }

   private:
    // one substep of the actual equations
    void substep(double torque_nm, double dt_s);

    // how much of the commanded torque the actuator really delivers this instant
    double deliverable(double torque_nm) const;

    PlantParams p_{};
    double angle_ = 0.0;
    double w_platform_ = 0.0;
    double w_wheel_ = 0.0;
};

}  // namespace fsw::sil

#endif  // FSW_SIL_PLANT_HPP
