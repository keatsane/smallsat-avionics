/**
 * @file   command_handler.hpp
 * @brief  processes incoming command messages and dispatches relevant responses
 */

#ifndef FSW_COMMS_COMMAND_HANDLER_HPP
#define FSW_COMMS_COMMAND_HANDLER_HPP

#include <cstdint>

#include "etl/circular_buffer.h"
#include "protocol/msg.hpp"
#include "protocol/state.hpp"

namespace fsw {

// timeout length in milliseconds (5 x heartbeat/NOOP keep alive of 1Hz)
inline constexpr uint32_t kCommandTimeoutMs = 5000;

// why a command was rejected (Ok = accepted) - maps to command_ack_t.reason
enum class CmdReject : uint8_t {
    Ok,             // accepted - passed every check
    UnknownId,      // cmd_id is not in the catalog
    IllegalInMode,  // command not allowed in the current mode
    BadArg,         // argument out of range for this command
};

// per-command policy, indexed by the command's id (its position in FSW_COMMAND_LIST in state.hpp)
struct CommandSpec {
    uint8_t legal_modes;  // bit m set = command allowed in Mode m (REQ-CMD-001)
    const char* req_id;   // requirement this command's handling serves (traceability)
};

// one row of the command event log - accepted or rejected and why
struct CommandEvent {
    uint32_t t_ms;     // platform time the command was handled
    uint8_t cmd_id;    // which command - raw, since an unknown id may not be a valid Command
    bool accepted;     // true if it passed validation and was dispatched
    CmdReject reason;  // why it was rejected (Ok when accepted)
};

// fixed-capacity ring of the kLogCapacity most recent commands - no heap, oldest overwritten
using CommandLog = etl::circular_buffer<CommandEvent, kLogCapacity>;

class CommandHandler {
   public:
    /**
     * @brief  handle an incoming command message
     * @param  cmd   		the incoming command id, argument, and sequence
     * @param  current_mode	the current active mode of the state machine
     * @param  t_ms			platform time the command arrived (ms since boot)
     * @return the generated command event, logged into the command log
     */
    CommandEvent handle(const command_t& cmd, Mode current_mode, uint32_t t_ms);

    /**
     * @brief  determines if the command link has been lost (silence longer than the timeout)
     * @param  t_ms   	  platform time (ms since boot)
     * @return true if the link is lost, false while in contact
     */
    bool link_lost(uint32_t t_ms) const;

    /** @brief look up a commands's static policy - legal modes and owning requirement */
    const CommandSpec& command_spec(Command c) const;

    /** @brief the command history, oldest to newest (read-only) */
    const CommandLog& log() const { return log_; }

   private:
    CommandLog log_;
    uint32_t last_command_ms_ = 0;
};

}  // namespace fsw

#endif  // FSW_COMMS_COMMAND_HANDLER_HPP
