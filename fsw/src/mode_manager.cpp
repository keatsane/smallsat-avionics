#include "fsw/mode_manager.hpp"

namespace fsw {

ModeManager::ModeManager() : current_(Mode::BOOT) {}

bool ModeManager::request(Mode to, Trigger trigger, uint32_t t_ms, const char* req_id) {
    // TODO(phase2): reject illegal transitions per the mode table before recording
    log_.push(ModeTransition{t_ms, trigger, current_, to, req_id});
    current_ = to;
    return true;
}

}  // namespace fsw
