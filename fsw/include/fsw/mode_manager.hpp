/**
 * @file   mode_manager.hpp
 * @brief  owns the current mode and the transition history (REQ-MODE-001/002)
 */

#ifndef FSW_MODE_MANAGER_HPP
#define FSW_MODE_MANAGER_HPP

#include <cstdint>

#include "etl/circular_buffer.h"
#include "protocol/state.hpp"

namespace fsw {

// what caused a transition - logged with every mode change. internal only, never on the wire
enum class Trigger : uint8_t {
    PowerOn,       // first power-on or reset into BOOT
    Nominal,       // autonomous advance along the normal flow
    FaultEntry,    // a fault forced the change (usually -> SAFE)
    FaultCleared,  // a fault was cleared, returning toward normal ops
    Timeout,       // a mode's dwell timer expired
    Command,       // a ground command drove it
};

// one row of the mode-transition log - the full record of a single accepted transition
// (REQ-MODE-008)
struct ModeTransition {
    uint32_t t_ms;       // platform time at the transition
    Trigger trigger;     // why it happened
    Mode from;           // previous mode
    Mode to;             // new mode
    const char* req_id;  // requirement it satisfies, e.g. "REQ-FAULT-002"
};

// fixed-capacity ring of the kLogCapacity most recent transitions - no heap, oldest overwritten
// (REQ-MODE-009)
using ModeLog = etl::circular_buffer<ModeTransition, kLogCapacity>;

class ModeManager {
   public:
    /** @brief the mode the spacecraft is in right now */
    Mode mode() const { return current_; }

    /**
     * @brief  transition to @p to if it's legal, recording the cause and the requirement it serves
     * @param  to       target mode
     * @param  trigger  what drove the change
     * @param  t_ms     platform time of the transition
     * @param  req_id   requirement this transition satisfies
     * @return true if the transition was legal and taken; false if rejected (nothing logged)
     */
    bool request(Mode to, Trigger trigger, uint32_t t_ms, const char* req_id);

    /** @brief the transition history, oldest to newest (read-only) */
    const ModeLog& log() const { return log_; }

   private:
    Mode current_ = Mode::BOOT;  // starts in BOOT on construction (REQ-MODE-002)
    ModeLog log_;
};

}  // namespace fsw

#endif  // FSW_MODE_MANAGER_HPP
