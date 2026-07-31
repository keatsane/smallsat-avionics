/**
 * @file   command_handler.cpp
 * @brief  processes incoming command messages and dispatches relevant responses
 */

#include "fsw/comms/command_handler.hpp"

#include "fsw/mode_manager.hpp"  // mode_transition_legal - SET_MODE is validated against it

namespace fsw {
namespace {

// legal in every mode - the low kModeCount bits (0b111111 for 6 modes)
constexpr uint8_t kAllModes = static_cast<uint8_t>((1u << kModeCount) - 1);

// command table: legal_modes and req_id
constexpr CommandSpec kCommandTable[kCommandCount] = {
    /* NOOP          */ {kAllModes, "REQ-CMD-001"},
    /* SET_MODE      */ {kAllModes, "REQ-CMD-001"},
    /* CLEAR_FAULT   */ {kAllModes, "REQ-FAULT-010"},
    /* CAPTURE_IMAGE */ {mode_bit(Mode::POINTING), "REQ-CMD-001"},
    /* SET_HEADING   */ {mode_bit(Mode::POINTING), "REQ-ADCS-002"},
};

static_assert(sizeof(kCommandTable) / sizeof(kCommandTable[0]) == kCommandCount,
              "command table out of sync with FSW_COMMAND_LIST");

// is this mode id something the ground may ask for? BOOT is not: it means "powered on and still
// self-checking", a state the vehicle enters by resetting and leaves by passing its checks, so
// there is nothing for the flight software to do with a request to re-enter it. accepting one and
// then silently not moving is worse than refusing it - the ack would say yes while nothing
// happened (REQ-CMD-005)
constexpr bool commandable_mode(uint8_t arg) {
    return arg < kModeCount && static_cast<Mode>(arg) != Mode::BOOT;
}

}  // namespace

CommandEvent CommandHandler::handle(const command_t& cmd, Mode current_mode, uint32_t t_ms) {
    const uint32_t since_last = t_ms - last_command_ms_;
    last_command_ms_ = t_ms;

    // a repeat of the previous sequence number is a retransmission, not a new command. the radio
    // link is half duplex and the vehicle is deaf while its beacon is in the air, so the ground
    // resends anything it has not seen acknowledged (REQ-CMD-003) - and without this, the resend
    // that the ground sent because the *ack* was lost would execute the command a second time.
    //
    // the previous answer is replayed rather than recomputed, because recomputing gives a
    // different one: a SET_MODE that already took effect is no longer a legal transition from the
    // mode it produced, so the ground would get a rejection for a command that worked
    if (have_last_seq_ && cmd.seq == last_seq_ && since_last <= kCommandDedupMs) {
        CommandEvent dup = last_event_;
        dup.t_ms = t_ms;
        dup.duplicate = true;
        log_.push(dup);
        return dup;
    }

    CmdReject reason = CmdReject::Ok;

    if (cmd.cmd_id >= kCommandCount) {
        reason = CmdReject::UnknownId;
    } else {
        const Command c = static_cast<Command>(cmd.cmd_id);  // id is real past this point
        if ((command_spec(c).legal_modes & mode_bit(current_mode)) == 0) {
            reason = CmdReject::IllegalInMode;
        } else if ((c == Command::SET_MODE && !commandable_mode(cmd.arg)) ||
                   (c == Command::CLEAR_FAULT && cmd.arg >= kFaultCount) ||
                   (c == Command::CAPTURE_IMAGE && cmd.arg >= kResolutionCount)) {
            reason = CmdReject::BadArg;
        } else if (c == Command::SET_MODE && !mode_transition_legal(current_mode, Trigger::Command,
                                                                    static_cast<Mode>(cmd.arg))) {
            // the target is a real mode but not reachable from this one (REQ-MODE-003), and that
            // is knowable here rather than only after the mode manager shrugs. same reasoning as
            // the BOOT refusal above: an ack saying yes while the vehicle does not move is a worse
            // interface than a refusal that names why (REQ-CMD-006).
            //
            // this does not make acceptance mean execution - a safing raised in the same cycle
            // still overrides an accepted command (REQ-EXEC-001, SIL-008). it closes the narrower
            // case where the refusal was already predictable at validation time
            reason = CmdReject::IllegalInMode;
        }
    }

    CommandEvent ce{t_ms, cmd.cmd_id, reason == CmdReject::Ok, reason, false};
    log_.push(ce);

    last_event_ = ce;
    last_seq_ = cmd.seq;
    have_last_seq_ = true;
    return ce;
}

bool CommandHandler::link_lost(uint32_t t_ms) const {
    return ((t_ms - last_command_ms_) > kCommandTimeoutMs);
}

const CommandSpec& CommandHandler::command_spec(Command c) const {
    return kCommandTable[static_cast<size_t>(c)];
}

}  // namespace fsw
