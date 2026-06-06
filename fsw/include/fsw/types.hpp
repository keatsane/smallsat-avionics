/**
 * @file   types.hpp
 * @brief  flight-software-only types - the mode-transition record and its trigger
 *
 * the Mode and Fault enums themselves are the wire contract, so they live in the shared
 * common/protocol/state.h (included below) rather than here.
 */

#ifndef FSW_TYPES_HPP
#define FSW_TYPES_HPP

#include <cstdint>

#include "protocol/state.h"  // canonical fsw::Mode + fsw::Fault, shared with the firmware

namespace fsw {

// what caused a transition - logged with every mode change. internal only, never on the wire
enum class Trigger : uint8_t {
    PowerOn,
    Command,
    FaultEntry,
    FaultCleared,
    Timeout,
};

// one row of the mode-transition log (REQ-MODE-002)
struct ModeTransition {
    uint32_t t_ms;       // platform time at the transition
    Trigger trigger;     // why it happened
    Mode from;           // previous mode
    Mode to;             // new mode
    const char* req_id;  // requirement it satisfies, e.g. "REQ-FAULT-002"
};

}  // namespace fsw

#endif  // FSW_TYPES_HPP
