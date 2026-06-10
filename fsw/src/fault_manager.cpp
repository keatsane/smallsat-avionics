/**
 * @file   fault_manager.cpp
 * @brief  latches faults and decides when one forces SAFE (REQ-FAULT-001/002)
 */

#include "fsw/fault_manager.hpp"

namespace fsw {
namespace {

// fault table: severity, debounce_n, req_id
constexpr FaultSpec kFaultTable[] = {
    /* SENSOR_DISAGREEMENT */ {Severity::Warning, 3, "REQ-FAULT-001"},
    /* ACTUATOR_SATURATION */ {Severity::Warning, 5, "REQ-FAULT-001"},
    /* UNDERTEMPERATURE    */ {Severity::Warning, 5, "REQ-FAULT-001"},
    /* GYRO_BIAS_DRIFT     */ {Severity::Warning, 10, "REQ-FAULT-001"},
    /* COMMAND_LINK_LOSS   */ {Severity::Critical, 1, "REQ-CMD-002"},  // command-loss dead man
    /* DATA_STALE          */ {Severity::Degraded, 3, "REQ-FAULT-005"},
    /* GYRO_DROPOUT        */ {Severity::Degraded, 3, "REQ-FAULT-005"},
    /* MAG_DROPOUT         */ {Severity::Degraded, 3, "REQ-FAULT-005"},
    /* WATCHDOG_TIMEOUT    */ {Severity::Critical, 1, "REQ-FAULT-002"},
    /* BUS_FAULT           */ {Severity::Critical, 1, "REQ-FAULT-002"},
    /* UNDERVOLTAGE        */ {Severity::Critical, 3, "REQ-FAULT-002"},
    /* OVERVOLTAGE         */ {Severity::Critical, 3, "REQ-FAULT-002"},
    /* OVERCURRENT         */ {Severity::Critical, 3, "REQ-FAULT-002"},
    /* OVERTEMPERATURE     */ {Severity::Critical, 5, "REQ-FAULT-002"},
};
static_assert(sizeof(kFaultTable) / sizeof(kFaultTable[0]) == kFaultCount,
              "fault table is out of sync with FSW_FAULT_LIST in state.hpp");

// the set of faults that force SAFE when latched: Critical severity (REQ-FAULT-002), folded out of
// the table once at compile time so the table stays the only place this policy is written - add a
// Critical fault row and this mask updates itself
constexpr uint32_t forces_safe_mask() {
    uint32_t m = 0;
    for (uint8_t i = 0; i < kFaultCount; ++i) {
        if (kFaultTable[i].severity == Severity::Critical) {
            m |= (1u << i);
        }
    }
    return m;
}
constexpr uint32_t kForcesSafe = forces_safe_mask();

}  // namespace

void FaultManager::set(Fault f, uint32_t t_ms) {
    if (is_active(f)) return;  // already latched - no edge, no duplicate event
    active_ |= fault_bit(f);
    log_.push(FaultEvent{t_ms, f, true});
}

void FaultManager::clear(Fault f, uint32_t t_ms) {
    if (!is_active(f)) return;  // wasn't latched - nothing to clear or log
    active_ &= ~fault_bit(f);
    log_.push(FaultEvent{t_ms, f, false});
}

bool FaultManager::is_active(Fault f) const { return (active_ & fault_bit(f)) != 0; }

bool FaultManager::should_enter_safe() const {
    // any latched fault whose policy forces SAFE -> command SAFE (REQ-FAULT-002)
    return (active_ & kForcesSafe) != 0;
}

void FaultManager::update(Fault f, bool bad, uint32_t t_ms) {
    const auto id = static_cast<size_t>(f);
    if (bad) {
        if (streak_[id] < kFaultTable[id].debounce_n) streak_[id]++;  // count this bad sample
        if (streak_[id] >= kFaultTable[id].debounce_n) set(f, t_ms);  // hit threshold -> latch
    } else {
        streak_[id] = 0;  // a good sample breaks the streak, but does NOT clear latch
    }
}

const FaultSpec& FaultManager::fault_spec(Fault f) const {
    return kFaultTable[static_cast<size_t>(f)];
}

}  // namespace fsw
