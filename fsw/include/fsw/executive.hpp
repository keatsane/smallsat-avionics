/**
 * @file   executive.hpp
 * @brief  runs once per control cycle, feeds data into fault detection and applies mode decision
 */

#ifndef FSW_EXECUTIVE_HPP
#define FSW_EXECUTIVE_HPP

#include "fsw/comms/command_handler.hpp"
#include "fsw/comms/telemetry_producer.hpp"
#include "fsw/fault_manager.hpp"
#include "fsw/inputs.hpp"
#include "fsw/mode_manager.hpp"
#include "fsw/platform.hpp"
#include "fsw/sensor_monitor.hpp"
#include "protocol/frame.hpp"
#include "protocol/msg.hpp"

namespace fsw {

class Executive {
   public:
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

    /**
     * @brief  inhibit a fault's autonomous response for a bench run (REQ-FAULT-012)
     * @param  f  the fault to inhibit
     *
     * the only writable door into the fault manager, and deliberately narrow: it cannot set,
     * clear, or hide a fault, only stop one from commanding a mode change. the caller is
     * responsible for declaring what it inhibited
     */
    void inhibit_fault(Fault f) { fm_.inhibit(f, true); }

    /** @brief the mode manager */
    const ModeManager& modes() const { return mm_; }

   private:
    // cycles BOOT waits before declaring the self-check passed. matches the longest fault debounce
    // in the table, so a persistently bad sensor has had every chance to latch first (REQ-MODE-010)
    static constexpr uint8_t kBootCheckCycles = 3;
    uint8_t boot_cycles_ = 0;

    // image chunks put on the link per cycle in DOWNLINK. four is a budget, not a preference: the
    // uart's transmit ring is 256 bytes and telemetry already spends ~80 of it each cycle, so a
    // larger burst would sit blocked in uart_write waiting for the ring to drain. at this rate a
    // 7 KB frame takes about three seconds, which is also long enough to watch happen
    static constexpr uint8_t kPayloadChunksPerCycle = 4;

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
    SensorMonitor sm_;
};

}  // namespace fsw

#endif  // FSW_EXECUTIVE_HPP
