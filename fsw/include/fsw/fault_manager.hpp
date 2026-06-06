/**
 * @file   fault_manager.hpp
 * @brief  latches faults and decides when one forces SAFE (REQ-FAULT-001/002)
 */

#ifndef FSW_FAULT_MANAGER_HPP
#define FSW_FAULT_MANAGER_HPP

#include <cstdint>

#include "fsw/types.hpp"

namespace fsw {

// pure logic: sensor samples + time are passed in, so faults are tested deterministically.
// active_ is the "1 << fault id" bitmask summary that ships in heartbeat_t.faults; the full
// per-fault table (debounce, severity, latch state) is the next step built on top of this.
class FaultManager {
   public:
    void set(Fault f);
    void clear(Fault f);

    bool is_active(Fault f) const;
    uint32_t active() const { return active_; }  // bitmask for heartbeat_t.faults

    /**
     * @brief  feed one undervoltage sample; latch only after enough consecutive bad ones
     * @param  bus_below_threshold  true when this sample reads under the limit
     * @param  t_ms                 platform time of the sample
     *
     * TODO(phase2): debounce so a single noisy sample can't latch the fault
     */
    void update_undervoltage(bool bus_below_threshold, uint32_t t_ms);

    // true when a latched fault is critical and has no documented degraded mode, so the
    // mode manager should command SAFE.
    // TODO(phase2): drive this from the fault table's per-fault severity
    bool should_enter_safe() const;

   private:
    uint32_t active_ = 0;
};

}  // namespace fsw

#endif  // FSW_FAULT_MANAGER_HPP
