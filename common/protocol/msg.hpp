/**
 * @file   msg.hpp
 * @brief  message ids and payload layouts carried over the link
 */

#ifndef COMMON_PROTOCOL_MSG_HPP
#define COMMON_PROTOCOL_MSG_HPP

#include <cstdint>

namespace fsw {

// message type that tags each frame
enum class MsgId : uint8_t {
    // control plane - uplink + its ack
    Command = 0x01,
    CommandAck = 0x02,

    // system telemetry - link + node health
    Heartbeat = 0x03,
    UartStatus = 0x04,
    // LoraStatus = 0x05,   // reserved - lora radio health (rfm95): rssi, snr, crc fails
    // Nrf24Status = 0x06,  // reserved - nrf24 radio health (nrf24l01+): retransmits, lost packets

    // sensor telemetry
    ImuData = 0x07,    // icm-20948 accel/gyro/mag
    PowerData = 0x08,  // ina228 bus voltage/current/power
    // TempData = 0x09,   // reserved - tmp117 structural temp

    // payload - bulk data, downlink mode only (0x10 block leaves room for the category)
    // PayloadData = 0x10,  // reserved - chunked image buffer over the payload link
};

// ------- control plane -------

// MsgId::Command - a ground -> spacecraft command
struct __attribute__((packed)) command_t {
    uint8_t cmd_id;  // which command (fsw::Command in state.hpp)
    uint8_t arg;   // unused for NOOP/CAPTURE_IMAGE, mode id for SET_MODE, fault id for CLEAR_FAULT
    uint16_t seq;  // ground-assigned; echoed in the ack so dropped/duplicate commands show
};

// MsgId::CommandAck - spacecraft -> ground reply to every command (REQ-CMD-003)
struct __attribute__((packed)) command_ack_t {
    uint8_t cmd_id;    // the command being acknowledged
    uint16_t seq;      // echoes command_t.seq so the ground matches reply to request
    uint8_t accepted;  // 1 = accepted and executed, 0 = rejected
    uint8_t reason;    // why it was rejected (CmdReject in command_handler.hpp; 0 when accepted)
};

// ------- system telemetry -------

// MsgId::Heartbeat - periodic "node is alive" beacon
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

// lora_status_t  - reserved (rssi, snr, crc fails), lands with the lora (rfm95) transport
// nrf24_status_t - reserved (retransmits, lost packets), lands with the nrf24 transport

// ------- sensor telemetry -------

// imu_data_t.flags bits
inline constexpr uint8_t kImuFlagAccelGyroValid = 0x01;  // accel + gyro sample is good
inline constexpr uint8_t kImuFlagMagValid = 0x02;        // magnetometer sample is good

// MsgId::ImuData - one icm-20948 sample (accel + gyro + mag), raw counts
struct __attribute__((packed)) imu_data_t {
    uint32_t t_ms;     // acquisition time (ms since boot)
    int16_t accel[3];  // accelerometer raw x, y, z
    int16_t gyro[3];   // gyroscope raw x, y, z
    int16_t mag[3];    // magnetometer raw x, y, z
    uint8_t flags;     // kImuFlag* bits - bit 0 accel/gyro valid, bit 1 mag valid
};

// power_data_t.flags bit
inline constexpr uint8_t kPowerFlagValid = 0x01;

// MsgId::PowerData - one ina228 sample (bus voltage/current/power)
struct __attribute__((packed)) power_data_t {
    uint32_t t_ms;       // acquisition time
    uint32_t bus_mv;     // bus voltage (millivolts)
    int32_t current_ma;  // current (milliamps)
    uint32_t power_mw;   // power (milliwatts)
    uint8_t flags;       // kPowerFlag* bits - bit 0 validity, + tbd
};

// temp_data_t  - reserved (tmp117 structural temp)

// ------- payload -------

// payload_data_t - reserved (chunked image buffer over the payload link)

// wire layout guards - these sizes are the contract the ground decodes against, so a dropped
// packed attribute or a changed field fails the build instead of silently breaking the link
static_assert(sizeof(command_t) == 4, "command_t wire layout changed");
static_assert(sizeof(command_ack_t) == 5, "command_ack_t wire layout changed");
static_assert(sizeof(heartbeat_t) == 11, "heartbeat_t wire layout changed");
static_assert(sizeof(uart_status_t) == 16, "uart_status_t wire layout changed");
// static_assert(sizeof(lora_status_t) == ?, "lora_status_t wire layout changed");
// static_assert(sizeof(nrf24_status_t) == ?, "nrf24_status_t wire layout changed");
static_assert(sizeof(imu_data_t) == 23, "imu_data_t wire layout changed");
static_assert(sizeof(power_data_t) == 17, "power_data_t wire layout changed");
// static_assert(sizeof(temp_data_t) == ?, "temp_data_t wire layout changed");

}  // namespace fsw

#endif  // COMMON_PROTOCOL_MSG_HPP
