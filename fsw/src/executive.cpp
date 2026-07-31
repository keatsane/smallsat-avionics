/**
 * @file   executive.cpp
 * @brief  runs once per control cycle, feeds fault detection and applies the mode decision
 */

#include "fsw/executive.hpp"

namespace fsw {

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

    // imu degraded fallback
    if ((mm_.mode() == Mode::POINTING || mm_.mode() == Mode::DETUMBLE) &&
        fm_.response_active(Fault::ACCEL_GYRO_DROPOUT)) {
        mm_.request(Mode::STANDBY, Trigger::FaultEntry, t_ms,
                    fm_.fault_spec(Fault::ACCEL_GYRO_DROPOUT).req_id);
    }

    // power-monitor degraded fallback: with power visibility lost, the high-power modes run blind
    // to brownout/overcurrent, so retreat them to STANDBY - lower draw, less risk (REQ-FAULT-005)
    if ((mm_.mode() == Mode::POINTING || mm_.mode() == Mode::DETUMBLE ||
         mm_.mode() == Mode::DOWNLINK) &&
        fm_.response_active(Fault::POWER_DROPOUT)) {
        mm_.request(Mode::STANDBY, Trigger::FaultEntry, t_ms,
                    fm_.fault_spec(Fault::POWER_DROPOUT).req_id);
    }

    // wheel degraded fallback: pointing and detumble are the two modes that actuate, and with the
    // wheel link down a torque command goes nowhere - so retreat to STANDBY rather than hold a
    // mode the rig cannot fly (REQ-FAULT-005)
    if ((mm_.mode() == Mode::POINTING || mm_.mode() == Mode::DETUMBLE) &&
        fm_.response_active(Fault::WHEEL_DROPOUT)) {
        mm_.request(Mode::STANDBY, Trigger::FaultEntry, t_ms,
                    fm_.fault_spec(Fault::WHEEL_DROPOUT).req_id);
    }

    // dispatch accepted ground commands. acceptance only means the command passed validation
    if (inputs.command && ce.accepted) {
        const Command cmd = static_cast<Command>(ce.cmd_id);
        switch (cmd) {
            case Command::NOOP:
                break;
            case Command::SET_MODE:
                if (!entered_safe_this_cycle) {  // never override a safing from the same cycle
                    mm_.request(static_cast<Mode>(inputs.command->arg), Trigger::Command, t_ms,
                                ch_.command_spec(cmd).req_id);
                }
                break;
            case Command::CLEAR_FAULT:
                fm_.clear(static_cast<Fault>(inputs.command->arg), t_ms);
                break;
            case Command::CAPTURE_IMAGE:
                // fire-and-forget - the frame lands in the camera's fifo and is reported back as
                // camera telemetry, so nothing here waits on the payload (REQ-PAY-001)
                platform::capture_image();
                break;
        }
    }

    // boot info - offered on the first cycle only, so this fires once per reset (REQ-WDG-002).
    // sent ahead of the heartbeat deliberately: a ground station reading the log top-down should
    // learn why the computer restarted before it sees the first state it restarted into
    if (inputs.boot) {
        send(MsgId::BootInfo, *inputs.boot);
    }

    // heartbeat
    if (tp_.heartbeat_due(t_ms)) {
        send(MsgId::Heartbeat, tp_.heartbeat(t_ms, mm_.mode(), fm_.active(), fm_.inhibited()));
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
