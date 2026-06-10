/**
 * @file   telemetry_producer.hpp
 * @brief  produces telemetry based on the current state of the satellite
 */

#ifndef FSW_COMMS_TELEMETRY_PRODUCER_HPP
#define FSW_COMMS_TELEMETRY_PRODUCER_HPP

#include <cstdint>

#include "fsw/comms/command_handler.hpp"
#include "protocol/msg.hpp"
#include "protocol/state.hpp"

namespace fsw {

// heartbeat rate = 1Hz (1/second)
inline constexpr uint32_t kHeartbeatIntervalMs = 1000;

class TelemetryProducer {
   public:
    /**
     * @brief  builds a heartbeat message and stamps it as sent
     * @param  t_ms   platform time (ms since boot) - carried as heartbeat_t.uptime_ms
     * @param  mode   current mode (fsw::Mode in state.hpp)
     * @param  faults bitmask of active faults (1 << fault id from state.hpp)
     * @return the generated heartbeat message
     */
    heartbeat_t heartbeat(uint32_t t_ms, Mode mode, uint32_t faults);

    /**
     * @brief  determines if a heartbeat is due
     * @param  t_ms platform time (ms since boot)
     * @return true if a full interval has passed since the last heartbeat; the first
     *         heartbeat comes due one interval after boot
     */
    bool heartbeat_due(uint32_t t_ms) const;

    /**
     * @brief  builds the reply to a handled command
     * @param  cmd the command being acknowledged - cmd_id and seq are echoed
     * @param  ce  the validation verdict from the command handler
     * @return the generated ack message
     */
    command_ack_t ack(const command_t& cmd, const CommandEvent& ce) const;

   private:
    uint16_t heartbeat_seq_ = 0;
    uint32_t last_heartbeat_ms_ = 0;
};

}  // namespace fsw

#endif  // FSW_COMMS_TELEMETRY_PRODUCER_HPP
