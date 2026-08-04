/**
 * @file   executive.hpp
 * @brief  runs once per control cycle, feeds data into fault detection and applies mode decision
 */

#ifndef FSW_EXECUTIVE_HPP
#define FSW_EXECUTIVE_HPP

#include "fsw/attitude_control.hpp"
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

    /** @brief the attitude controller - its gains and whether it is saturating */
    const AttitudeControl& control() const { return ac_; }

   private:
    // cycles BOOT waits before declaring the self-check passed. matches the longest fault debounce
    // in the table, so a persistently bad sensor has had every chance to latch first (REQ-MODE-010)
    static constexpr uint8_t kBootCheckCycles = 3;
    uint8_t boot_cycles_ = 0;

    // last cycle's mode and time - the attitude controller needs to know when POINTING was
    // entered, and how much time to integrate heading over
    Mode last_mode_ = Mode::BOOT;
    uint32_t last_t_ms_ = 0;
    bool ran_ = false;
    uint16_t last_bus_mv_ = 0;  // newest valid power reading, the heartbeat vital sign

    // consecutive in-deadband samples seen in DETUMBLE - the mode's exit condition, debounced
    // for the same reason fault entry is (REQ-MODE-011). 10 cycles is one second at the control
    // rate, long enough that a rate passing through zero mid-oscillation does not count as done
    static constexpr uint16_t kDetumbleDoneCycles = 10;
    uint16_t detumble_done_cycles_ = 0;

    // consecutive over-threshold samples seen outside DETUMBLE - the autonomous entry's debounce
    // (REQ-MODE-012). three cycles, so a single bumped sample does not yank the vehicle out of a
    // downlink pass
    static constexpr uint16_t kDetumbleEnterCycles = 3;
    uint16_t detumble_enter_cycles_ = 0;

    // where an autonomous DETUMBLE should return to when it finishes. only an *autonomous* entry
    // resumes - a commanded DETUMBLE exits to STANDBY, because the ground asking for it is the
    // ground taking over the plan
    Mode resume_mode_ = Mode::STANDBY;
    bool resume_valid_ = false;

    // PULSE_WHEEL's outstanding test point: the torque to hold and the time it stops. bounded in
    // here rather than by the ground remembering to cancel it, and abandoned if the vehicle leaves
    // STANDBY mid-pulse
    float pulse_torque_nm_ = 0.0F;
    uint32_t pulse_until_ms_ = 0;

    // largest body rate since that pulse began - the test point's actual result, measured where
    // the gyro is rather than inferred from a telemetry stream ten times slower than the event
    float pulse_peak_rads_ = 0.0F;

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
    AttitudeControl ac_;
};

}  // namespace fsw

#endif  // FSW_EXECUTIVE_HPP
