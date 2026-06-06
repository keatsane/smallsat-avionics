/**
 * @file   state.h
 * @brief  canonical spacecraft mode + fault ids - the single source of truth
 *
 * the firmware (C) and the flight software (C++) both include this header, so the mode
 * and fault definitions live in exactly one place instead of being repeated on each side.
 * the values are the wire contract carried in heartbeat_t (msg.h): the mode in
 * heartbeat_t.mode, and a "1 << fault id" bitmask in heartbeat_t.faults.
 */

#ifndef PROTOCOL_STATE_H
#define PROTOCOL_STATE_H

#include <stdint.h>

// spacecraft operating modes - X(name, value)
#define FSW_MODE_LIST(X) \
    X(BOOT, 0)           \
    X(STANDBY, 1)        \
    X(DETUMBLE, 2)       \
    X(POINTING, 3)       \
    X(DOWNLINK, 4)       \
    X(SAFE, 5)

// fault ids - X(name). a fault's order here is its id and its bit in heartbeat_t.faults
#define FSW_FAULT_LIST(X)  \
    X(UNDERVOLTAGE)        \
    X(OVERCURRENT)         \
    X(OVERVOLTAGE)         \
    X(OVERTEMPERATURE)     \
    X(UNDERTEMPERATURE)    \
    X(GYRO_DROPOUT)        \
    X(GYRO_BIAS_DRIFT)     \
    X(MAG_DROPOUT)         \
    X(SENSOR_DISAGREEMENT) \
    X(ACTUATOR_SATURATION) \
    X(COMMAND_TIMEOUT)     \
    X(TELEMETRY_LINK_LOSS) \
    X(WATCHDOG_TIMEOUT)    \
    X(BUS_FAULT)           \
    X(DATA_STALE)

#ifdef __cplusplus

namespace fsw {

enum class Mode : uint8_t {
#define FSW_MODE_X(name, value) name = value,
    FSW_MODE_LIST(FSW_MODE_X)
#undef FSW_MODE_X
};

enum class Fault : uint8_t {
#define FSW_FAULT_X(name) name,
    FSW_FAULT_LIST(FSW_FAULT_X)
#undef FSW_FAULT_X
};

// number of defined faults - e.g. the size of a fault table indexed by Fault
inline constexpr uint8_t kFaultCount =
#define FSW_FAULT_X(name) +1
    FSW_FAULT_LIST(FSW_FAULT_X);
#undef FSW_FAULT_X

}  // namespace fsw

#else  // plain C (firmware)

typedef enum {
#define FSW_MODE_X(name, value) MODE_##name = value,
    FSW_MODE_LIST(FSW_MODE_X)
#undef FSW_MODE_X
} mode_t;

typedef enum {
#define FSW_FAULT_X(name) FAULT_##name,
    FSW_FAULT_LIST(FSW_FAULT_X)
#undef FSW_FAULT_X
        FAULT_COUNT
} fault_id_t;

#endif  // __cplusplus

// name lookups - shared by C and C++, generated from the same lists
static inline const char* fsw_mode_name(uint8_t mode) {
    static const char* const names[] = {
#define FSW_MODE_X(name, value) #name,
        FSW_MODE_LIST(FSW_MODE_X)
#undef FSW_MODE_X
    };
    return mode < (uint8_t)(sizeof(names) / sizeof(names[0])) ? names[mode] : "UNKNOWN";
}

static inline const char* fsw_fault_name(uint8_t id) {
    static const char* const names[] = {
#define FSW_FAULT_X(name) #name,
        FSW_FAULT_LIST(FSW_FAULT_X)
#undef FSW_FAULT_X
    };
    return id < (uint8_t)(sizeof(names) / sizeof(names[0])) ? names[id] : "UNKNOWN";
}

#endif  // PROTOCOL_STATE_H
