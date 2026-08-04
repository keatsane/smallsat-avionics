/**
 * @file   executive.cpp
 * @brief  runs once per control cycle, feeds fault detection and applies the mode decision
 */

#include "fsw/executive.hpp"

#include <cmath>

namespace fsw {

namespace {

// below this the wheel is unwound enough to stop bleeding it - a deadband, so a wheel resting
// near zero does not sit commanding a trickle of torque forever.
//
// 5 rad/s and not 2, because the number this is compared against is not trustworthy near zero.
// The ESC's velocity estimate reads +/-3.1 rad/s with the wheel visibly stopped and its angle
// frozen (measured 2026-08-04) - it is a difference of quantised angles over a tiny interval, and
// at rest that is all noise. At 2 rad/s the dump fired on that noise, in whichever direction the
// noise happened to point, so a vehicle sitting in STANDBY spun its wheel *up* as often as down
// and every manoeuvre started from an arbitrary wheel state. The magnetometer gate learned this
// same lesson an hour earlier and switched to the angle; this one just needs to clear the floor.
constexpr int32_t kDumpDoneMrads = 5000;  // 5 rad/s - above the standstill noise, well under the
                                          // 36 rad/s limit, so a real full wheel still unwinds

// what to push while unwinding. it has to be under the platform's breakaway or the dump turns
// into a slew: the measured threshold for platform motion is ~5 mN m commanded, so 3 is inside
// it with margin. slow on purpose - a full wheel unwinds in a few seconds, which is nothing
// against the time a vehicle spends idle
constexpr float kDumpTorqueNm = 0.003F;

// oppose the wheel's spin, or nothing if it is already unwound. the sign is the wheel's own:
// slowing a wheel means torquing against the way it is turning, which is the one place in this
// flight software where the command opposes rather than follows
float dump_torque(int32_t wheel_mrad_s) {
    if (wheel_mrad_s > kDumpDoneMrads) {
        return -kDumpTorqueNm;
    }
    if (wheel_mrad_s < -kDumpDoneMrads) {
        return kDumpTorqueNm;
    }
    return 0.0F;
}

}  // namespace
namespace {

// radians to the wire's milliradians, clamped rather than wrapped: a heading that has run past
// +/-32 rad is a vehicle that has spun 5 times, and pinning the dial at the edge says that more
// honestly than a number that silently wraps round
int16_t to_mrad(float rad) {
    const float mr = rad * 1000.0F;
    if (mr > 32767.0F) {
        return 32767;
    }
    if (mr < -32768.0F) {
        return -32768;
    }
    return static_cast<int16_t>(mr);
}

}  // namespace

void Executive::cycle(const Inputs& inputs, uint32_t t_ms) {
    // ingest inputs
    for (const FaultReport& report : inputs.fault_updates) {
        fm_.update(report.fault, report.bad, t_ms);
    }

    // detect sensor faults from this cycle's readings, feeding the same fault manager (REQ-SNS-002)
    sm_.evaluate(inputs, fm_, t_ms);

    CommandEvent ce{};
    if (inputs.command) {
        ce = ch_.handle(*inputs.command, mm_.mode(), t_ms);
        send(MsgId::CommandAck, tp_.ack(*inputs.command, ce));  // every command answered
    }

    // sample command-link health
    fm_.update(Fault::COMMAND_LINK_LOSS, ch_.link_lost(t_ms), t_ms);

    bool entered_safe_this_cycle = false;

    // drive fault detection (update mode to safe if necessary)
    if ((mm_.mode() != Mode::SAFE) && fm_.should_enter_safe()) {
        entered_safe_this_cycle =
            mm_.request(Mode::SAFE, Trigger::FaultEntry, t_ms, "REQ-FAULT-002");
    }

    // leave BOOT once every detector has had a full debounce window to disqualify the vehicle and
    // nothing Critical latched. BOOT is not a resting state - without this the rig sits there
    // forever whenever no ground station is commanding it (REQ-MODE-010)
    if (mm_.mode() == Mode::BOOT) {
        if (boot_cycles_ < kBootCheckCycles) {
            boot_cycles_++;
        } else if (!fm_.should_enter_safe()) {
            mm_.request(Mode::STANDBY, Trigger::Nominal, t_ms, "REQ-MODE-010");
        }
    }

    // degraded fallbacks, one table row per (fault, modes it disqualifies) - REQ-FAULT-005.
    // the reasoning per row: losing the imu blinds the two modes that steer; losing the power
    // monitor blinds the three high-draw modes to brownout; losing the wheel makes a torque
    // command go nowhere. every retreat is to STANDBY, deliberately - a fallback that picked
    // fancier destinations would be a second mode ladder to keep correct
    struct Fallback {
        Fault fault;
        uint8_t from_modes;  // bit m set = this fault disqualifies Mode m
    };
    static constexpr uint8_t kActive = mode_bit(Mode::POINTING) | mode_bit(Mode::DETUMBLE);
    static constexpr Fallback kFallbacks[] = {
        {Fault::ACCEL_GYRO_DROPOUT, kActive},
        {Fault::POWER_DROPOUT, static_cast<uint8_t>(kActive | mode_bit(Mode::DOWNLINK))},
        {Fault::WHEEL_DROPOUT, kActive},
        // a full wheel is the same shape of problem as a missing one - the modes that steer have
        // no actuator left - and it retreats the same way. the difference is that this one has a
        // cure: STANDBY unwinds the wheel against the bearing, so the retreat is also where the
        // recovery happens rather than merely where the vehicle waits
        {Fault::WHEEL_SATURATED, kActive},
    };
    for (const Fallback& fb : kFallbacks) {
        if ((fb.from_modes & mode_bit(mm_.mode())) != 0U && fm_.response_active(fb.fault)) {
            mm_.request(Mode::STANDBY, Trigger::FaultEntry, t_ms, fm_.fault_spec(fb.fault).req_id);
        }
    }

    // dispatch accepted ground commands. acceptance only means the command passed validation, and
    // a duplicate is a retransmission that was already acted on - it is answered, never re-run
    if (inputs.command && ce.accepted && !ce.duplicate) {
        const Command cmd = static_cast<Command>(ce.cmd_id);
        switch (cmd) {
            case Command::NOOP:
                break;
            case Command::SET_MODE:
                if (!entered_safe_this_cycle) {  // never override a safing from the same cycle
                    mm_.request(static_cast<Mode>(inputs.command->arg), Trigger::Command, t_ms,
                                ch_.command_spec(cmd).req_id);
                    // any commanded mode change resets the autonomous-detumble bookkeeping: the
                    // ground has taken over the plan, so there is nothing to resume to
                    resume_valid_ = false;
                }
                break;
            case Command::CLEAR_FAULT:
                fm_.clear(static_cast<Fault>(inputs.command->arg), t_ms);
                break;
            case Command::REQUEST_TELEMETRY:
                // which frame ids exist on the wire is the platform's business, not validated
                // here - an id nothing ever emits simply never matches, and the ground command
                // catalog is what keeps the operator inside the useful set
                platform::poll_telemetry(inputs.command->arg);
                break;
            case Command::SET_HEADING:
                // a binary angle over the wire, radians in here - the conversion belongs at the
                // edge, like every other unit that crosses this boundary
                ac_.set_target(static_cast<float>(inputs.command->arg) * kHeadingStepRad);
                break;
            case Command::CAPTURE_IMAGE:
                // fire-and-forget - the frame lands in the camera's fifo and is reported back as
                // camera telemetry, so nothing here waits on the payload (REQ-PAY-001)
                platform::capture_image(inputs.command->arg);
                break;
            case Command::PULSE_WHEEL:
                // a signed byte of milli-newton-metres, held for a fixed window and then dropped.
                // bounded in the flight software rather than by the ground remembering to send a
                // zero: a standing torque nobody cancels is how a wheel walks into saturation
                pulse_torque_nm_ =
                    static_cast<float>(static_cast<int8_t>(inputs.command->arg)) / 1000.0F;
                pulse_until_ms_ = t_ms + kWheelPulseMs;
                pulse_peak_rads_ = 0.0F;  // this test point's answer starts blank
                break;
        }
    }

    // attitude control - DETUMBLE bleeds rate away, POINTING holds a heading (REQ-ADCS-001/002).
    //
    // commanded every cycle including zero, rather than only when there is something to do: the
    // wheel is a physical thing that keeps whatever torque it was last given, so leaving a mode
    // without sending a zero would walk it into saturation on a vehicle nobody is steering. the
    // rate is only acted on when an attitude sensor actually reported this cycle - no sample is
    // not the same as a rate of zero, and treating it as zero would command a stop the vehicle
    // has no evidence it needs
    const Mode mode_now = mm_.mode();

    // capture the heading to hold the moment POINTING is entered, however it was entered - a
    // commanded transition and a retreat back into it both have to reset the reference, and
    // watching the mode change catches every path without each one remembering to
    if (mode_now == Mode::POINTING && last_mode_ != Mode::POINTING) {
        ac_.enter_pointing();
    }
    last_mode_ = mode_now;

    // the cycle period is nominal, not measured, so integrate against what actually elapsed. a
    // late cycle that pretends it was on time integrates the wrong amount of heading
    const float dt_s = ran_ ? (static_cast<float>(t_ms - last_t_ms_) / 1000.0F) : 0.0F;
    last_t_ms_ = t_ms;
    ran_ = true;

    // the heading estimate advances in every mode, not only the ones that act on it - that is
    // what lets the heading survive mode changes and the ground watch the platform turn in
    // STANDBY. only estimation happens unconditionally; torque stays mode-gated below
    if (inputs.body_rate_rads) {
        ac_.update(*inputs.body_rate_rads, dt_s, inputs.mag_heading_rad);

        // the peak rate since the last pulse, watched at the control rate. the platform's answer
        // to a test point is a transient - it breaks loose, turns, and friction stops it, all
        // inside a second - so a stream sampled slower than that reports whatever phase it
        // happened to catch. this is the one number that says whether the bearing let go
        const float mag =
            (*inputs.body_rate_rads < 0.0F) ? -*inputs.body_rate_rads : *inputs.body_rate_rads;
        if (mag > pulse_peak_rads_) {
            pulse_peak_rads_ = mag;
        }
    }

    float torque_nm = 0.0F;

    // an actuator test point outranks the mode's own law, because it is only legal in STANDBY,
    // where there is no law running. it expires on its own clock and is abandoned if the vehicle
    // leaves STANDBY for any reason - a fault retreat mid-pulse must not leave torque standing
    const bool pulsing = (mode_now == Mode::STANDBY) && (pulse_until_ms_ != 0U) &&
                         static_cast<int32_t>(pulse_until_ms_ - t_ms) > 0;
    if (!pulsing) {
        pulse_until_ms_ = 0U;
        pulse_torque_nm_ = 0.0F;
    }

    if (pulsing) {
        torque_nm = pulse_torque_nm_;
    } else if (inputs.body_rate_rads && mode_now == Mode::DETUMBLE) {
        torque_nm = ac_.detumble(*inputs.body_rate_rads);

        // declare detumble done once the rate has sat inside the deadband for a full second
        // of consecutive samples (REQ-MODE-011). an autonomous entry resumes the mode it
        // interrupted (REQ-MODE-012); a commanded one steps down to STANDBY. autonomous on
        // purpose either way: a vehicle that nulls its rates and then waits for a human has
        // turned a recovery mode into a parking brake. one bad sample resets the count, same
        // shape as the fault debounce and for the same reason
        if (std::fabs(*inputs.body_rate_rads) < ac_.gains().rate_deadband) {
            if (detumble_done_cycles_ < kDetumbleDoneCycles) {
                detumble_done_cycles_++;
            } else if (resume_valid_) {
                resume_valid_ = false;
                mm_.request(resume_mode_, Trigger::Nominal, t_ms, "REQ-MODE-012");
            } else {
                mm_.request(Mode::STANDBY, Trigger::Nominal, t_ms, "REQ-MODE-011");
            }
        } else {
            detumble_done_cycles_ = 0;
        }
    } else if (inputs.body_rate_rads && mode_now == Mode::POINTING) {
        torque_nm = ac_.point(*inputs.body_rate_rads);
    } else if (mode_now == Mode::STANDBY && inputs.wheel) {
        // momentum dumping, and the bearing that makes pointing hard is what makes it possible.
        //
        // a reaction wheel only exchanges momentum, so a vehicle that has slewed one way has a
        // wheel carrying the balance and no authority left in that direction. Real spacecraft
        // dump against magnetorquers or thrusters; this one has friction. Any torque under the
        // platform's breakaway spins the wheel back down without moving the platform at all - the
        // stiction holds the body still while the wheel unwinds against it.
        //
        // STANDBY only, and that is the whole design: DETUMBLE and POINTING are steering and must
        // not have their actuator quietly drained underneath them, so the vehicle unwinds while
        // idle. WHEEL_SATURATED's fallback retreats to exactly here, which makes the retreat a
        // recovery rather than a parking space.
        torque_nm = dump_torque(inputs.wheel->velocity_mrad_s);
    }
    if (mode_now != Mode::DETUMBLE) {
        detumble_done_cycles_ = 0;  // a fresh DETUMBLE earns its exit from zero
    }

    // the autonomous half of DETUMBLE: a vehicle that starts spinning in any operating mode
    // pulls itself into the recovery, remembers where it was, and update()'s exit path above
    // returns it there (REQ-MODE-012). SAFE is deliberately not in the list - a safed vehicle
    // sheds activity, and spinning quietly is what SAFE already accepts
    //
    // POINTING is not in the list either, and that one was learned on the rig (2026-08-04). a slew
    // to a new bearing *is* body rate, deliberately, and it passes this threshold long before it
    // reaches the target - so the vehicle kept interrupting its own manoeuvre. POINTING commanded
    // a turn, the turn tripped this entry, DETUMBLE cancelled the turn, the resume put it back in
    // POINTING, and the two thrashed for forty seconds without ever arriving. detumble answers
    // rate nobody asked for; in POINTING the rate is the controller's own doing, and the pointing
    // law carries its own damping term to keep it in hand
    if (inputs.body_rate_rads && mode_now != Mode::DETUMBLE && mode_now != Mode::SAFE &&
        mode_now != Mode::BOOT && mode_now != Mode::POINTING &&
        std::fabs(*inputs.body_rate_rads) > ac_.gains().detumble_enter_rads) {
        if (detumble_enter_cycles_ < kDetumbleEnterCycles) {
            detumble_enter_cycles_++;
        } else if (mm_.request(Mode::DETUMBLE, Trigger::Nominal, t_ms, "REQ-MODE-012")) {
            resume_mode_ = mode_now;
            resume_valid_ = true;
            detumble_enter_cycles_ = 0;
        }
    } else {
        detumble_enter_cycles_ = 0;
    }
    platform::set_wheel_torque_nm(torque_nm);

    // boot info - offered on the first cycle only, so this fires once per reset (REQ-WDG-002).
    // sent ahead of the heartbeat deliberately: a ground station reading the log top-down should
    // learn why the computer restarted before it sees the first state it restarted into
    if (inputs.boot) {
        send(MsgId::BootInfo, *inputs.boot);
    }

    // the latest bus voltage, remembered across cycles - power samples arrive at 10 Hz and the
    // heartbeat leaves at 1 Hz, so the vital sign rides whichever sample was newest
    if (inputs.power && (inputs.power->flags & kPowerFlagValid) != 0U) {
        last_bus_mv_ = static_cast<uint16_t>(
            (inputs.power->bus_mv > 0xFFFFU) ? 0xFFFFU : inputs.power->bus_mv);
    }

    // heartbeat, and the pointing picture alongside it. the same cadence deliberately: they are
    // both "where is the vehicle right now", and a dial updating at a different rate to the mode
    // beside it reads as one of them being stale
    if (tp_.heartbeat_due(t_ms)) {
        send(MsgId::Heartbeat,
             tp_.heartbeat(t_ms, mm_.mode(), fm_.active(), fm_.inhibited(), last_bus_mv_));
    }

    // the pointing picture goes out every cycle, not at the heartbeat's rate. a 10 Hz control loop
    // cannot be tuned or verified from 1 Hz samples: on this rig a detumble is over in about a
    // third of a second, so the whole event fell between two telemetry lines and the only thing
    // visible was where it ended up. the radio still carries one a second - the beacon decimates
    // this stream - so the extra rate is spent on the wired link, where air time is not a budget
    attitude_status_t a{};
    a.t_ms = t_ms;
    a.heading_mrad = to_mrad(ac_.heading());
    a.target_mrad = to_mrad(ac_.target());
    a.rate_mrads = to_mrad(inputs.body_rate_rads ? *inputs.body_rate_rads : 0.0F);
    a.torque_mnm = static_cast<int16_t>(torque_nm * 1000.0F);
    a.pulse_peak_mrads = to_mrad(pulse_peak_rads_);
    a.flags = static_cast<uint8_t>((ac_.pointing_in_band() ? kAttitudeFlagInBand : 0U) |
                                   (ac_.saturated() ? kAttitudeFlagSaturated : 0U) |
                                   (inputs.mag_heading_rad ? 0U : kAttitudeFlagGyroOnly));
    send(MsgId::AttitudeStatus, a);

    // the wheel's own report, forwarded rather than consumed silently. the executive has been
    // reading these to decide WHEEL_DROPOUT since the ESC arrived and never passing them on, so
    // the actuator was the one subsystem the ground could be told had failed without ever being
    // shown its state. it is also the only place the wheel's speed is visible, which is what
    // turns a commanded torque into a measured one: the wheel's angular acceleration against its
    // known inertia is the actuator's real output, and the way to find `kOhmsPerKt`
    if (inputs.wheel) {
        send(MsgId::WheelStatus, *inputs.wheel);
    }

    // imu data
    if (inputs.imu) {
        send(MsgId::ImuData, *inputs.imu);
    }

    // power data
    if (inputs.power) {
        send(MsgId::PowerData, *inputs.power);
    }

    // temp data
    if (inputs.temp) {
        send(MsgId::TempData, *inputs.temp);
    }

    // camera health - the frame itself is bulk data and downlinks separately
    if (inputs.camera) {
        send(MsgId::CameraStatus, *inputs.camera);
    }

    // DOWNLINK is the mode that empties the payload buffer, and this is the whole of what makes
    // it different from STANDBY. said every cycle rather than only on the edges, so dropping out
    // of DOWNLINK - including a safing retreat - stops the stream without needing its own hook.
    // how fast the buffer drains is the platform's call: the rate is a property of the link, and
    // a number chosen here would be this side of the boundary guessing at the other (REQ-PAY-004)
    platform::set_payload_downlink(mm_.mode() == Mode::DOWNLINK);
}

}  // namespace fsw
