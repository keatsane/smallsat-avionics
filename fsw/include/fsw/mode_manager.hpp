/**
 * @file   mode_manager.hpp
 * @brief  owns the current mode and the transition history (REQ-MODE-001/002)
 */

#ifndef FSW_MODE_MANAGER_HPP
#define FSW_MODE_MANAGER_HPP

#include "etl/circular_buffer.h"
#include "fsw/types.hpp"

namespace fsw {

// the mode-change history is a fixed-capacity ring (no heap, from ETL) so the same code runs
// on the host and the target; it keeps the most recent transitions and overwrites the oldest
using ModeLog = etl::circular_buffer<ModeTransition, 32>;

// pure logic: time is passed in, nothing here talks to hardware. that keeps the whole
// state machine unit-testable by feeding it a timeline and reading back the log
class ModeManager {
   public:
    ModeManager();

    Mode mode() const { return current_; }

    /**
     * @brief  transition to @p to, recording the cause and the requirement it serves
     * @param  to       target mode
     * @param  trigger  what drove the change
     * @param  t_ms     platform time of the transition
     * @param  req_id   requirement this transition satisfies
     * @return true if the transition was taken
     *
     * TODO(phase2): enforce the legal-transition table; for now every request is taken
     */
    bool request(Mode to, Trigger trigger, uint32_t t_ms, const char* req_id);

    const ModeLog& log() const { return log_; }

   private:
    Mode current_;
    ModeLog log_;
};

}  // namespace fsw

#endif  // FSW_MODE_MANAGER_HPP
