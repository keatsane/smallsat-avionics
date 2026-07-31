/**
 * @file   executive.cpp
 * @brief  runs once per control cycle, feeds fault detection and applies the mode decision
 */

#include "fsw/executive.hpp"

#include <cmath>

namespace fsw {
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
    }

    float torque_nm = 0.0F;
    if (inputs.body_rate_rads && mode_now == Mode::DETUMBLE) {
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
    }
    if (mode_now != Mode::DETUMBLE) {
        detumble_done_cycles_ = 0;  // a fresh DETUMBLE earns its exit from zero
    }

    // the autonomous half of DETUMBLE: a vehicle that starts spinning in any operating mode
    // pulls itself into the recovery, remembers where it was, and update()'s exit path above
    // returns it there (REQ-MODE-012). SAFE is deliberately not in the list - a safed vehicle
    // sheds activity, and spinning quietly is what SAFE already accepts
    if (inputs.body_rate_rads && mode_now != Mode::DETUMBLE && mode_now != Mode::SAFE &&
        mode_now != Mode::BOOT &&
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

        attitude_status_t a{};
        a.t_ms = t_ms;
        a.heading_mrad = to_mrad(ac_.heading());
        a.target_mrad = to_mrad(ac_.target());
        a.rate_mrads = to_mrad(inputs.body_rate_rads ? *inputs.body_rate_rads : 0.0F);
        a.torque_mnm = static_cast<int16_t>(torque_nm * 1000.0F);
        a.flags = static_cast<uint8_t>((ac_.pointing_in_band() ? kAttitudeFlagInBand : 0U) |
                                       (ac_.saturated() ? kAttitudeFlagSaturated : 0U));
        send(MsgId::AttitudeStatus, a);
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
