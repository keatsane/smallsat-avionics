/**
 * @file   telemetry_producer.cpp
 * @brief  produces telemetry based on the current state of the satellite
 */

#include "fsw/comms/telemetry_producer.hpp"

namespace fsw {

heartbeat_t TelemetryProducer::heartbeat(uint32_t t_ms, Mode mode, uint32_t faults) {
    last_heartbeat_ms_ = t_ms;
    return {t_ms, static_cast<uint8_t>(mode), faults, heartbeat_seq_++};
}

bool TelemetryProducer::heartbeat_due(uint32_t t_ms) const {
    return ((t_ms - last_heartbeat_ms_) >= kHeartbeatIntervalMs);
}

command_ack_t TelemetryProducer::ack(const command_t& cmd, const CommandEvent& ce) const {
    return {cmd.cmd_id, cmd.seq, static_cast<uint8_t>(ce.accepted ? 1 : 0),
            static_cast<uint8_t>(ce.reason)};
}

}  // namespace fsw
