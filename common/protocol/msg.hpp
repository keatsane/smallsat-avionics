/**
 * @file   msg.hpp
 * @brief  message ids and payload layouts carried over the link
 */

#ifndef COMMON_PROTOCOL_MSG_HPP
#define COMMON_PROTOCOL_MSG_HPP

#include <cstdint>

#include "protocol/frame.hpp"  // kFrameMaxPayload - the ceiling every message must fit under

namespace fsw {

// message type that tags each frame
enum class MsgId : uint8_t {
    // control plane - uplink + its ack
    Command = 0x01,
    CommandAck = 0x02,

    // system telemetry - link + node health
    Heartbeat = 0x03,
    UartStatus = 0x04,
    LoraStatus = 0x05,   // rfm95 beacon health
    Nrf24Status = 0x06,  // nrf24l01+ payload-link health

    // sensor telemetry
    ImuData = 0x07,    // icm-20948 accel/gyro/mag
    PowerData = 0x08,  // ina228 bus voltage/current/power
    TempData = 0x09,   // tmp117 structural temp

    // payload - bulk data, downlink mode only (0x10 block leaves room for the category)
    PayloadData = 0x10,     // one chunk of a captured image
    CameraStatus = 0x11,    // arducam link health + how much frame is waiting in its fifo
    DownlinkStatus = 0x12,  // how far a payload downlink has got, and what the radio did with it
    GroundStatus = 0x13,    // the ground station talking about itself, not about the spacecraft

    // adcs (0x14 block)
    AttitudeStatus = 0x14,  // where the vehicle is pointing and where it was told to point

    ChunkRequest = 0x15,  // ground -> spacecraft: resend these payload chunks (selective repeat)

    // actuation - the reaction-wheel node, on its own link rather than the ground one (0x20 block)
    WheelCommand = 0x20,  // obc -> esc torque setpoint
    WheelStatus = 0x21,   // esc -> obc wheel state

    // rtos / platform health (0x30 block) - about the computer rather than the spacecraft
    TaskHealth = 0x30,  // per-task liveness and stack margin
    BootInfo = 0x31,    // why the computer last reset, sent once per boot
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

// ------- actuation -------

// MsgId::WheelCommand - obc -> esc q-axis voltage setpoint
struct __attribute__((packed)) wheel_command_t {
    int16_t torque_mv;  // q-axis volts x1000, signed - the sign sets spin direction
    uint16_t seq;       // obc-assigned, echoed back in wheel_status_t
};

inline constexpr uint8_t kWheelFlagFocReady = 0x01;  // foc aligned - the wheel can be driven
inline constexpr uint8_t kWheelFlagSensorOk = 0x02;  // the as5600 acked on i2c
inline constexpr uint8_t kWheelFlagMagnetOk = 0x04;  // as5600 status MD - it can see the magnet
inline constexpr uint8_t kWheelFlagDriverOk = 0x08;  // the 6-pwm driver came up - pwm is running

// MsgId::WheelStatus - esc -> obc, sent on every command and periodically between them
struct __attribute__((packed)) wheel_status_t {
    int32_t velocity_mrad_s;  // shaft velocity, milliradians per second
    int32_t angle_mrad;       // shaft angle, milliradians
    int16_t torque_mv;        // what is actually applied - zero after a dead-man timeout
    uint8_t flags;            // kWheelFlag* bits
    uint16_t seq;             // echoes the command that set this target
};

// ------- system telemetry -------

// MsgId::Heartbeat - periodic "node is alive" beacon
struct __attribute__((packed)) heartbeat_t {
    uint32_t uptime_ms;  // milliseconds since boot
    uint8_t mode;        // current mode (fsw::Mode in state.hpp)
    uint32_t faults;     // bitmask of active faults (1 << fault id from state.hpp)
    uint32_t inhibited;  // same bit layout - faults whose response is suppressed (REQ-FAULT-012)
    uint16_t seq;        // increments each heartbeat, so drops are visible

    // bus voltage in millivolts, 0 when no power sample has ever arrived. in the heartbeat
    // rather than left to the POWER frame because on battery this is a vital sign, not a
    // curiosity - the ground station's screen needs it without polling for it
    uint16_t bus_mv;
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
    uint8_t flags;       // kPowerFlag* bits - bit 0 validity
};

// temp_data_t.flags bit
inline constexpr uint8_t kTempFlagValid = 0x01;

// MsgId::TempData - one tmp117 sample (temperature)
struct __attribute__((packed)) temp_data_t {
    uint32_t t_ms;    // acquisition time
    int32_t temp_mc;  // temperature (milli-degrees celsius)
    uint8_t flags;    // kTempFlag* bits - bit 0 validity
};

// ------- payload -------

// camera_data_t.flags bits
inline constexpr uint8_t kCameraFlagValid = 0x01;       // the arducam answered its test register
inline constexpr uint8_t kCameraFlagFrameReady = 0x02;  // a captured frame is waiting in the fifo
inline constexpr uint8_t kCameraFlagCapturing = 0x04;   // a capture is in flight

// MsgId::CameraStatus - payload camera health. the frame itself goes out as payload_data_t, so
// this is the small, always-on part the ground can read every pass
struct __attribute__((packed)) camera_data_t {
    uint32_t t_ms;         // acquisition time
    uint32_t frame_bytes;  // still waiting in the fifo, 0 when there is no frame
    uint8_t flags;         // kCameraFlag* bits - bit 0 validity
};

// bytes of image per chunk. sized so payload_data_t fills the frame's 64-byte payload exactly -
// the header below is 8 bytes, and the framer refuses anything larger
inline constexpr uint8_t kPayloadChunkBytes = 56;

// MsgId::PayloadData - one chunk of a captured image.
//
// self-describing on purpose: every chunk carries which image it belongs to, its index, and how
// many there are, so a receiver can reassemble out of order, detect a gap, and know when a frame
// is complete without being told separately. that is what makes a multi-pass downlink possible -
// a pass that ends halfway through an image is a set of chunks, not a broken stream
struct __attribute__((packed)) payload_data_t {
    uint16_t image_id;  // increments per capture, so two images never interleave silently
    uint16_t chunk;     // 0-based index of this chunk within the image
    uint16_t chunks;    // total chunks in this image
    uint8_t len;        // valid bytes in data - only the last chunk is ever short
    uint8_t reserved;   // pad to a 2-byte boundary, keeping the struct's layout explicit
    uint8_t data[kPayloadChunkBytes];
};

// MsgId::DownlinkStatus - progress of the payload downlink currently in flight.
//
// the chunks themselves go out on the nRF24, which is the link that cannot report on itself: if
// nothing arrives, nothing arrives, and the ground has no way to tell a vehicle that never started
// from a radio that swallowed every packet. so this rides the LoRa beacon instead - a different
// transport, small, and only while a downlink is actually running.
//
// it is a progress bar and an instrument in one. `chunk`/`chunks` is how far along the image is;
// `radio_sent` and `radio_dropped` are the packets the nRF24 accepted and the ones it refused for
// a full fifo, which is the difference between "the vehicle is not sending" and "the vehicle is
// sending and you are not hearing it"
struct __attribute__((packed)) downlink_status_t {
    uint32_t t_ms;
    uint16_t image_id;       // which image, matching payload_data_t.image_id
    uint16_t chunk;          // chunks handed to the links so far
    uint16_t chunks;         // total in this image, 0 when nothing is in flight
    uint32_t radio_sent;     // nrf24 packets accepted since boot
    uint16_t radio_dropped;  // nrf24 packets refused for a full fifo since boot, saturating
};

// ground_status_t.flags bits
inline constexpr uint8_t kGroundFlagLoraUp = 0x01;   // the ground station's lora came up at boot
inline constexpr uint8_t kGroundFlagNrf24Up = 0x02;  // and its nrf24

inline constexpr uint8_t kGroundFlagPanel1 = 0x04;  // the status oled answered on 0x3C
inline constexpr uint8_t kGroundFlagPanel2 = 0x08;  // the payload oled answered on 0x3D

// MsgId::GroundStatus - the ground station's own health, emitted onto usb once a second.
//
// every other message in this file is the spacecraft talking. this one is not, and it exists
// because the ground station used to say whether its radios came up exactly once, at boot, before
// anybody had opened a terminal. a receiver that failed to initialise then looked identical to a
// working one hearing nothing - which is precisely the state that hid a dead payload receiver
// while the vehicle happily transmitted a whole image into it
struct __attribute__((packed)) ground_status_t {
    uint32_t t_ms;         // ground station uptime, not the spacecraft's
    uint32_t lora_frames;  // whole frames recovered on each link since the ground station booted
    uint32_t nrf24_frames;
    uint32_t nrf24_packets;  // raw packets off the payload radio, before framing

    // share of carrier-detect samples that read high since the last report. this is the payload
    // channel's occupancy, not the vehicle's signal: 2476 MHz has wifi and bluetooth in it, so a
    // few percent is the resting state of a bench with nothing transmitting. what a real burst
    // looks like is this number climbing while packets do not
    uint8_t nrf24_busy_pct;

    uint8_t flags;  // kGroundFlag* bits
};

// attitude_status_t.flags bits
inline constexpr uint8_t kAttitudeFlagInBand = 0x01;     // holding the commanded bearing
inline constexpr uint8_t kAttitudeFlagSaturated = 0x02;  // the wheel is at its limit
// no absolute reference this cycle, so the heading is dead-reckoned from the gyro alone. says
// nothing about why - an invalid sample, a hard-iron calibration that has not seen a full turn,
// and a magnetometer held off because the wheel is spinning all land here. what matters to anyone
// reading a heading is whether it is anchored or drifting, and this is that bit
inline constexpr uint8_t kAttitudeFlagGyroOnly = 0x04;

// MsgId::AttitudeStatus - the pointing picture, in milliradians.
//
// heading is measured from wherever POINTING was entered rather than from anything absolute: the
// gyro gives rate and the magnetometer sits beside a motor full of magnets, so there is no north
// on this vehicle. it is integrated rate, which means it drifts, and a display drawing it should
// be read as "how far round from where it started" rather than as a bearing (REQ-ADCS-002)
struct __attribute__((packed)) attitude_status_t {
    uint32_t t_ms;
    int16_t heading_mrad;  // relative to the entry heading, integrated from the rate
    int16_t target_mrad;   // what SET_HEADING asked for, in the same frame
    int16_t rate_mrads;    // measured yaw rate
    int16_t torque_mnm;    // what the controller commanded the wheel, milli-newton-metres

    // the largest body rate seen since the last PULSE_WHEEL, and the reason it is here rather
    // than derived on the ground: a pulse lasts half a second and the radio carries one of these
    // a second, so whether the platform moved is a question the sampled stream answers by luck.
    // the flight software watches the gyro at the control rate and simply knows. cleared when a
    // new pulse starts, so it always describes the most recent test point
    int16_t pulse_peak_mrads;

    uint8_t flags;  // kAttitudeFlag* bits
};

// how many chunk indices one request can carry - sized so the struct fills the frame's 64-byte
// payload budget (2 + 1 + 2*28 = 59)
inline constexpr uint8_t kChunkRequestMax = 28;

// MsgId::ChunkRequest - the ground asking for the payload chunks it is missing.
//
// this is what makes the payload downlink a protocol rather than a hope. the nRF24 path is
// one-way and lossy, so the vehicle cannot know what arrived - but the LoRa uplink exists, and the
// ground knows exactly which chunks it lacks. so the image goes out once, the ground names its
// gaps, and the vehicle resends precisely those: selective repeat, with the request riding the
// other radio. a lost request costs nothing, because the ground repeats it until the image
// completes. unused entries beyond `count` are zero and ignored
struct __attribute__((packed)) chunk_request_t {
    uint16_t image_id;  // which image, matching payload_data_t.image_id
    uint8_t count;      // how many entries of chunks[] are meaningful
    uint16_t chunks[kChunkRequestMax];
};

// ------- rtos / platform health -------

// how many tasks one report can carry. sized for the whole planned set plus the kernel's idle task,
// so adding a task does not change the wire layout
inline constexpr uint8_t kTaskHealthMaxTasks = 7;

// one task's health. stack margin is in words because that is the unit the kernel counts stacks in
struct __attribute__((packed)) task_entry_t {
    uint8_t id;                 // which task (TaskId in the obc's task_health.hpp)
    uint8_t state;              // freertos eTaskState: 0 running, 1 ready, 2 blocked, 3 suspended
    uint16_t stack_free_words;  // smallest free stack ever seen, not the free stack right now
    uint16_t checkin_age_ms;    // since this task last finished a pass; 0xFFFF = never, or n/a
};

// task_health_t.flags bit - clear means the watchdog was deliberately left unfed this pass, which
// is the spacecraft announcing it is about to reset itself
inline constexpr uint8_t kTaskHealthFlagWatchdogFed = 0x01;

// MsgId::TaskHealth - liveness and stack margin for every task (REQ-RT-003).
//
// platform telemetry, not flight-software state: the fsw is single-threaded by design and never
// learns that tasks exist, so this is assembled and sent by the health task rather than by the
// executive. that split is what keeps Executive::cycle() identical between SIL and the target
struct __attribute__((packed)) task_health_t {
    uint32_t t_ms;  // when the snapshot was taken
    uint8_t count;  // valid entries in tasks
    uint8_t flags;  // kTaskHealthFlag* bits - bit 0 the watchdog was serviced this pass
    task_entry_t tasks[kTaskHealthMaxTasks];
};

// radio status flags - the same bit means the same thing for both radios
inline constexpr uint8_t kRadioFlagConfigured = 0x01;  // init succeeded at boot
inline constexpr uint8_t kRadioFlagAnswering = 0x02;   // and it still reads back its own registers

// MsgId::LoraStatus / MsgId::Nrf24Status - one per radio, same shape on purpose.
//
// every other subsystem on this vehicle reports whether it is alive; until this landed the two
// radios were the exception, so a dead one was invisible - no fault, no telemetry, nothing. these
// carry observability only. deliberately no fault yet: what a spacecraft should *do* about a dead
// beacon is a real decision, and a fault nothing responds to is just a catalog entry
struct __attribute__((packed)) radio_status_t {
    uint32_t t_ms;
    uint32_t sent;     // frames or packets handed to the radio since boot
    uint16_t dropped;  // offered and refused - a full fifo, or newer state replacing older
    uint8_t flags;     // kRadioFlag* bits
};

// MsgId::BootInfo - why the computer last reset, sent once per boot (REQ-WDG-002).
//
// the console banner has always printed this, but a banner scrolls away and a ground station that
// joined late never saw it. carrying it as a frame means a reset is attributable from the log
// alone - which matters most for the one cause nobody was watching when it happened, the watchdog
struct __attribute__((packed)) boot_info_t {
    uint32_t t_ms;        // when the frame went out, not when the reset happened
    uint32_t clk_hz;      // core clock the image actually came up at
    uint8_t reset_cause;  // fsw::ResetCause in state.hpp
};

// wire layout guards - these sizes are the contract the ground decodes against, so a dropped
// packed attribute or a changed field fails the build instead of silently breaking the link
static_assert(sizeof(command_t) == 4, "command_t wire layout changed");
static_assert(sizeof(command_ack_t) == 5, "command_ack_t wire layout changed");
static_assert(sizeof(heartbeat_t) == 17, "heartbeat_t wire layout changed");
static_assert(sizeof(uart_status_t) == 16, "uart_status_t wire layout changed");
// static_assert(sizeof(lora_status_t) == ?, "lora_status_t wire layout changed");
// static_assert(sizeof(nrf24_status_t) == ?, "nrf24_status_t wire layout changed");
static_assert(sizeof(imu_data_t) == 23, "imu_data_t wire layout changed");
static_assert(sizeof(power_data_t) == 17, "power_data_t wire layout changed");
static_assert(sizeof(temp_data_t) == 9, "temp_data_t wire layout changed");
static_assert(sizeof(camera_data_t) == 9, "camera_data_t wire layout changed");
static_assert(sizeof(payload_data_t) == 64, "payload_data_t wire layout changed");
static_assert(sizeof(downlink_status_t) == 16, "downlink_status_t wire layout changed");
static_assert(sizeof(ground_status_t) == 18, "ground_status_t wire layout changed");
static_assert(sizeof(attitude_status_t) == 15, "attitude_status_t wire layout changed");
static_assert(sizeof(chunk_request_t) == 59, "chunk_request_t wire layout changed");
static_assert(sizeof(payload_data_t) <= kFrameMaxPayload, "payload_data_t no longer fits a frame");
static_assert(sizeof(task_entry_t) == 6, "task_entry_t wire layout changed");
static_assert(sizeof(task_health_t) == 48, "task_health_t wire layout changed");
static_assert(sizeof(task_health_t) <= kFrameMaxPayload, "task_health_t no longer fits a frame");
static_assert(sizeof(boot_info_t) == 9, "boot_info_t wire layout changed");
static_assert(sizeof(radio_status_t) == 11, "radio_status_t wire layout changed");

}  // namespace fsw

#endif  // COMMON_PROTOCOL_MSG_HPP
