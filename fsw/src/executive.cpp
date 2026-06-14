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

    // imu degraded fallback
    if ((mm_.mode() == Mode::POINTING || mm_.mode() == Mode::DETUMBLE) &&
        fm_.is_active(Fault::ACCEL_GYRO_DROPOUT)) {
        mm_.request(Mode::STANDBY, Trigger::FaultEntry, t_ms,
                    fm_.fault_spec(Fault::ACCEL_GYRO_DROPOUT).req_id);
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
                // payload capture tbd
                break;
        }
    }

    // heartbeat
    if (tp_.heartbeat_due(t_ms)) {
        send(MsgId::Heartbeat, tp_.heartbeat(t_ms, mm_.mode(), fm_.active()));
    }

    // imu data
    if (inputs.imu) {
        send(MsgId::ImuData, *inputs.imu);
    }
}

}  // namespace fsw
