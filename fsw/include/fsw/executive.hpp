/**
 * @file   executive.hpp
 * @brief  runs once per control cycle, feeds data into fault detection and applies mode decision
 */

#ifndef FSW_EXECUTIVE_HPP
#define FSW_EXECUTIVE_HPP

#include <optional>

#include "etl/vector.h"
#include "fsw/comms/command_handler.hpp"
#include "fsw/comms/telemetry_producer.hpp"
#include "fsw/fault_manager.hpp"
#include "fsw/mode_manager.hpp"
#include "fsw/platform.hpp"
#include "protocol/frame.hpp"
#include "protocol/msg.hpp"

namespace fsw {

// fault and if its now active
struct FaultReport {
    Fault fault;  // fault
    bool bad;     // is this fault active
};

// new inputs to be passed into the next cycle
struct Inputs {
    etl::vector<FaultReport, kFaultCount> fault_updates;  // fault updates since last cycle
    std::optional<command_t> command;                     // incoming command (optional)
};

class Executive {
   public:
    // generate Inputs method to build the struct?

    /**
     * @brief  runs a full control cycle, handling commands, faults, and modes
     * @param  inputs incoming command (optional) + report of active faults since last cycle
     * @param  t_ms   platform time (ms since boot)
     */
    void cycle(const Inputs& inputs, uint32_t t_ms);

    /** @brief the telemetry producer */
    const TelemetryProducer& telemetry() const { return tp_; }

    /** @brief the command handler */
    const CommandHandler& commands() const { return ch_; }

    /** @brief the fault manager */
    const FaultManager& faults() const { return fm_; }

    /** @brief the mode manager */
    const ModeManager& modes() const { return mm_; }

   private:
    // wrap a wire message in a frame and hands it to the link
    template <typename T>
    void send(MsgId id, const T& msg) {
        static_assert(sizeof(T) <= kFrameMaxPayload, "message too large for one frame");
        uint8_t buf[kFrameMaxSize];
        const size_t n = frame_encode(static_cast<uint8_t>(id),
                                      reinterpret_cast<const uint8_t*>(&msg), sizeof(T), buf);
        platform::send_telemetry(buf, static_cast<uint32_t>(n));
    }

    CommandHandler ch_;
    TelemetryProducer tp_;
    FaultManager fm_;
    ModeManager mm_;
};

}  // namespace fsw

#endif  // FSW_EXECUTIVE_HPP
