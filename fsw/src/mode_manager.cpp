/**
 * @file   mode_manager.cpp
 * @brief  owns the current mode and the transition history (REQ-MODE-001/002)
 */

#include "fsw/mode_manager.hpp"

namespace fsw {
namespace {

// per-mode allow-list of transitions as a bitmask - for each source mode, bit `to` is set when
// (this mode -> to) is a legal autonomous transition; anything not set is illegal, including
// self-transitions (BOOT->BOOT) and any climb out of SAFE (that's ground-commanded, separate)
// every operating mode keeps a "-> SAFE" bit so a fault can always force safe mode
constexpr uint8_t kAutoAllowed[kModeCount] = {
    /* from BOOT     */ mode_bit(Mode::STANDBY) | mode_bit(Mode::DETUMBLE) | mode_bit(Mode::SAFE),
    /* from STANDBY  */ mode_bit(Mode::DETUMBLE) | mode_bit(Mode::SAFE),
    /* from DETUMBLE */ mode_bit(Mode::STANDBY) | mode_bit(Mode::POINTING) | mode_bit(Mode::SAFE),
    /* from POINTING */ mode_bit(Mode::STANDBY) | mode_bit(Mode::DOWNLINK) | mode_bit(Mode::SAFE),
    /* from DOWNLINK */ mode_bit(Mode::STANDBY) | mode_bit(Mode::POINTING) | mode_bit(Mode::SAFE),
    /* from SAFE     */ 0,  // no autonomous exit from SAFE - only a ground command climbs out
};
static_assert(sizeof(kAutoAllowed) / sizeof(kAutoAllowed[0]) == kModeCount,
              "mode transition table is out of sync with FSW_MODE_LIST in state.hpp");

bool is_legal(Mode from, Trigger trigger, Mode to) {
    if (static_cast<size_t>(to) >= kModeCount) {
        return false;  // out-of-range target
    }
    // a legal autonomous transition, or the one ground-commanded exit from SAFE (REQ-MODE-006)
    const bool autonomous = (kAutoAllowed[static_cast<size_t>(from)] & mode_bit(to)) != 0;
    const bool safe_recovery =
        (from == Mode::SAFE && trigger == Trigger::Command && to == Mode::STANDBY);
    return autonomous || safe_recovery;
}

}  // namespace

bool ModeManager::request(Mode to, Trigger trigger, uint32_t t_ms, const char* req_id) {
    if (!is_legal(current_, trigger, to)) {
        return false;  // illegal transition - don't log, don't change mode
    }
    log_.push(ModeTransition{t_ms, trigger, current_, to, req_id});
    current_ = to;
    return true;
}

}  // namespace fsw
