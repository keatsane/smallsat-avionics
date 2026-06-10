/**
 * @file   fault_manager.hpp
 * @brief  latches faults and decides when one forces SAFE (REQ-FAULT-001/002)
 */

#ifndef FSW_FAULT_MANAGER_HPP
#define FSW_FAULT_MANAGER_HPP

#include <cstdint>

#include "etl/circular_buffer.h"
#include "protocol/state.hpp"

namespace fsw {

// fault severity, ordered low to high - drives the response (REQ-FAULT-002)
enum class Severity : uint8_t {
    Warning,   // off-nominal but still fully capable - reported only, no action
    Degraded,  // a capability was lost; switch to a documented fallback, do not SAFE
    Critical,  // a capability was lost with no fallback - force SAFE
};

// per-fault policy, indexed by the fault's id (its position in FSW_FAULT_LIST in state.hpp)
struct FaultSpec {
    Severity severity;    // Critical -> SAFE, Degraded -> documented fallback, else report only
    uint16_t debounce_n;  // # of consecutive faulty samples before the fault latches
    const char* req_id;   // requirement this fault's handling serves (traceability)
};

// one row of the fault event log - a single latch or clear edge (REQ-FAULT-011)
struct FaultEvent {
    uint32_t t_ms;  // platform time of the edge
    Fault fault;    // which fault - the key back to fault_spec()
    bool latched;   // true = latched, false = cleared
};

// fixed-capacity ring of the kLogCapacity most recent edges - no heap, oldest overwritten
// (REQ-FAULT-011)
using FaultLog = etl::circular_buffer<FaultEvent, kLogCapacity>;

class FaultManager {
   public:
    /**
     * @brief  latch a fault now, regardless of debounce (commanded, or once update hits threshold)
     * @param  f     which fault to latch
     * @param  t_ms  platform time, stamped on the log edge
     */
    void set(Fault f, uint32_t t_ms);

    /**
     * @brief  clear a latched fault - the only thing that un-latches one (REQ-FAULT-010)
     * @param  f     which fault to clear
     * @param  t_ms  platform time, stamped on the log edge
     */
    void clear(Fault f, uint32_t t_ms);

    /**
     * @brief  feed one sample; latches after debounce_n consecutive bad ones (REQ-FAULT-004)
     * @param  f     which fault the sample is for
     * @param  bad   true if this sample is faulty; a good one resets the streak (REQ-FAULT-009)
     * @param  t_ms  platform time, stamped on the latch edge if this sample latches
     */
    void update(Fault f, bool bad, uint32_t t_ms);

    /** @brief true if fault @p f is currently latched */
    bool is_active(Fault f) const;

    /** @brief the latched-fault bitmask (bit n = fault id n), as carried in heartbeat_t.faults */
    uint32_t active() const { return active_; }

    /** @brief true when any latched fault is Critical, so the mode manager should command SAFE */
    bool should_enter_safe() const;

    /** @brief look up a fault's static policy - severity, debounce, owning requirement */
    const FaultSpec& fault_spec(Fault f) const;

    /** @brief the latch/clear edge history, oldest to newest (read-only) */
    const FaultLog& log() const { return log_; }

   private:
    uint32_t active_ = 0;                // latched faults, one bit per fault id
    uint16_t streak_[kFaultCount] = {};  // consecutive bad-sample count per fault (debounce state)
    FaultLog log_;                       // alongside active_ and streak_
};

}  // namespace fsw

#endif  // FSW_FAULT_MANAGER_HPP
