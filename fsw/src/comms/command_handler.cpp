/**
 * @file   command_handler.cpp
 * @brief  processes incoming command messages and dispatches relevant responses
 */

#include "fsw/comms/command_handler.hpp"

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
};

static_assert(sizeof(kCommandTable) / sizeof(kCommandTable[0]) == kCommandCount,
              "command table out of sync with FSW_COMMAND_LIST");

}  // namespace

CommandEvent CommandHandler::handle(const command_t& cmd, Mode current_mode, uint32_t t_ms) {
    last_command_ms_ = t_ms;
    CmdReject reason = CmdReject::Ok;

    if (cmd.cmd_id >= kCommandCount) {
        reason = CmdReject::UnknownId;
    } else {
        const Command c = static_cast<Command>(cmd.cmd_id);  // id is real past this point
        if ((command_spec(c).legal_modes & mode_bit(current_mode)) == 0) {
            reason = CmdReject::IllegalInMode;
        } else if ((c == Command::SET_MODE && cmd.arg >= kModeCount) ||
                   (c == Command::CLEAR_FAULT && cmd.arg >= kFaultCount)) {
            reason = CmdReject::BadArg;
        }
    }

    CommandEvent ce{t_ms, cmd.cmd_id, reason == CmdReject::Ok, reason};
    log_.push(ce);
    return ce;
}

bool CommandHandler::link_lost(uint32_t t_ms) const {
    return ((t_ms - last_command_ms_) > kCommandTimeoutMs);
}

const CommandSpec& CommandHandler::command_spec(Command c) const {
    return kCommandTable[static_cast<size_t>(c)];
}

}  // namespace fsw
