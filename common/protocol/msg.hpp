/**
 * @file   msg.hpp
 * @brief  message ids and payload layouts carried over the link
 */

#ifndef COMMON_PROTOCOL_MSG_HPP
#define COMMON_PROTOCOL_MSG_HPP

#include <cstdint>

namespace fsw {

// message type that tags each frame (cast to/from the uint8_t id on the wire)
enum class MsgId : uint8_t {
    Command = 0x01,     // ground -> spacecraft command (command_t)
    CommandAck = 0x02,  // spacecraft -> ground reply to a command (command_ack_t)
    Heartbeat = 0x03,   // periodic node-alive beacon (heartbeat_t)
    UartStatus = 0x04,  // uart transport receive quality (uart_status_t)
    // LoraStatus = 0x05,   reserved - lora radio status, arrives with the rfm95 transport
    // Nrf24Status = 0x06,  reserved - nrf24 radio status, arrives with the payload link
    // PayloadData = 0x07,
};

// ---------- uplink: ground -> spacecraft ----------

// MsgId::Command - a ground -> spacecraft command (little-endian on the wire)
struct __attribute__((packed)) command_t {
    uint8_t cmd_id;  // which command (fsw::Command in state.hpp)
    uint8_t arg;   // unused for NOOP/CAPTURE_IMAGE, mode id for SET_MODE, fault id for CLEAR_FAULT
    uint16_t seq;  // ground-assigned; echoed in the ack so dropped/duplicate commands show
};

// ---------- downlink: spacecraft -> ground, all telemetry ----------

// MsgId::CommandAck - spacecraft -> ground reply to every command (REQ-CMD-003)
struct __attribute__((packed)) command_ack_t {
    uint8_t cmd_id;    // the command being acknowledged
    uint16_t seq;      // echoes command_t.seq so the ground matches reply to request
    uint8_t accepted;  // 1 = accepted and executed, 0 = rejected
    uint8_t reason;    // why it was rejected (CmdReject in command_handler.hpp; 0 when accepted)
};

// MsgId::Heartbeat - periodic "node is alive" beacon (little-endian on the wire)
struct __attribute__((packed)) heartbeat_t {
    uint32_t uptime_ms;  // milliseconds since boot
    uint8_t mode;        // current mode (fsw::Mode in state.hpp)
    uint32_t faults;     // bitmask of active faults (1 << fault id from state.hpp)
    uint16_t seq;        // increments each heartbeat, so drops are visible
};

// MsgId::UartStatus - the uart transport's receive-side quality sent down as telemetry
struct __attribute__((packed)) uart_status_t {
    uint32_t overrun;  // bytes lost to an rx overrun
    uint32_t framing;  // uart framing errors
    uint32_t noise;    // uart noise-flag errors
    uint32_t dropped;  // frames discarded on a bad crc or resync
};

// lora_status_t (rssi, snr, crc fails) and nrf24_status_t (retransmits, lost packets) join here
// with their transports in phase 8

// payload_data_t - bulk payload data

// wire layout guards - these sizes are the contract the ground decodes against, so a dropped
// packed attribute or a changed field fails the build instead of silently breaking the link
static_assert(sizeof(command_t) == 4, "command_t wire layout changed");
static_assert(sizeof(command_ack_t) == 5, "command_ack_t wire layout changed");
static_assert(sizeof(heartbeat_t) == 11, "heartbeat_t wire layout changed");
static_assert(sizeof(uart_status_t) == 16, "uart_status_t wire layout changed");

}  // namespace fsw

#endif  // COMMON_PROTOCOL_MSG_HPP
