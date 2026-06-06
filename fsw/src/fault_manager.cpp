#include "fsw/fault_manager.hpp"

namespace fsw {

// a fault's id is its bit position in the active-fault bitmask
static uint32_t fault_bit(Fault f) { return 1u << static_cast<unsigned>(f); }

void FaultManager::set(Fault f) { active_ |= fault_bit(f); }

void FaultManager::clear(Fault f) { active_ &= ~fault_bit(f); }

bool FaultManager::is_active(Fault f) const { return (active_ & fault_bit(f)) != 0; }

void FaultManager::update_undervoltage(bool bus_below_threshold, uint32_t t_ms) {
    (void)t_ms;  // the debounce window will time off this
    // no debounce yet - latch on a single bad sample, clear on a good one. phase 2 adds
    // the "N consecutive samples over a window" guard so transients don't latch
    if (bus_below_threshold) {
        set(Fault::UNDERVOLTAGE);
    } else {
        clear(Fault::UNDERVOLTAGE);
    }
}

bool FaultManager::should_enter_safe() const {
    // TODO(phase2): a latched critical fault with no degraded mode -> SAFE (undervoltage first)
    return false;
}

}  // namespace fsw
