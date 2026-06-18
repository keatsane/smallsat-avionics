/**
 * @file   state.hpp
 * @brief  canonical spacecraft command, mode, and fault ids - the single source of truth
 *
 * these ids are the wire contract carried in msg.hpp: a command id in command_t.cmd_id, the
 * mode in heartbeat_t.mode, and a "1 << fault id" bitmask in heartbeat_t.faults
 */

#ifndef COMMON_PROTOCOL_STATE_HPP
#define COMMON_PROTOCOL_STATE_HPP

#include <cstdint>

// command ids - X(name). a command's order here is its id, carried in command_t.cmd_id
#define FSW_COMMAND_LIST(X)                                          \
    X(NOOP)          /* link keep-alive / test */                    \
    X(SET_MODE)      /* request a mode transition (arg = mode id) */ \
    X(CLEAR_FAULT)   /* clear a latched fault (arg = fault id) */    \
    X(CAPTURE_IMAGE) /* take a photo with the payload camera */

// fault ids - X(name). a fault's order here is its id and its bit in heartbeat_t.faults
#define FSW_FAULT_LIST(X)                                            \
    X(COMMAND_LINK_LOSS)  /* command-loss timer expired */           \
    X(ACCEL_GYRO_DROPOUT) /* accel/gyro invalid or frozen */         \
    X(MAG_DROPOUT)        /* magnetometer invalid or frozen */       \
    X(POWER_DROPOUT)      /* ina228 power monitor invalid/missing */ \
    X(UNDERVOLTAGE)       /* bus below the brownout threshold */     \
    X(OVERVOLTAGE)        /* bus above its max */                    \
    X(OVERCURRENT)        /* current draw over the limit */

// operating modes - X(name). a mode's order here is its id, carried in heartbeat_t.mode
#define FSW_MODE_LIST(X)                                      \
    X(BOOT)     /* power-on and self-checks */                \
    X(STANDBY)  /* idle and healthy, awaiting commands */     \
    X(DETUMBLE) /* reduce body rates after deploy or upset */ \
    X(POINTING) /* hold a commanded attitude */               \
    X(DOWNLINK) /* empty the data buffer to the ground */     \
    X(SAFE)     /* minimal, power-conservative fault state */

namespace fsw {

enum class Command : uint8_t {
#define FSW_COMMAND_X(name) name,
    FSW_COMMAND_LIST(FSW_COMMAND_X)
#undef FSW_COMMAND_X
};

enum class Fault : uint8_t {
#define FSW_FAULT_X(name) name,
    FSW_FAULT_LIST(FSW_FAULT_X)
#undef FSW_FAULT_X
};

enum class Mode : uint8_t {
#define FSW_MODE_X(name) name,
    FSW_MODE_LIST(FSW_MODE_X)
#undef FSW_MODE_X
};

// catalog sizes - one entry per list, counted at compile time
inline constexpr uint8_t kCommandCount =
#define FSW_COMMAND_X(name) +1
    FSW_COMMAND_LIST(FSW_COMMAND_X);
#undef FSW_COMMAND_X

inline constexpr uint8_t kFaultCount =
#define FSW_FAULT_X(name) +1
    FSW_FAULT_LIST(FSW_FAULT_X);
#undef FSW_FAULT_X

inline constexpr uint8_t kModeCount =
#define FSW_MODE_X(name) +1
    FSW_MODE_LIST(FSW_MODE_X);
#undef FSW_MODE_X

// the catalogs must fit their bitmask carriers (heartbeat_t.faults, CommandSpec.legal_modes)
static_assert(kFaultCount <= 32, "fault bitmask is a uint32_t - 32 faults max");
static_assert(kModeCount <= 8, "mode bitmasks are uint8_t - 8 modes max");

constexpr uint8_t kLogCapacity = 32;

// id -> single-bit mask, bit set at the id - fault_bit is the heartbeat_t.faults bit, mode_bit
// builds the mode allow-lists in the managers
inline constexpr uint32_t fault_bit(Fault f) { return 1u << static_cast<uint8_t>(f); }
inline constexpr uint8_t mode_bit(Mode m) {
    return static_cast<uint8_t>(1u << static_cast<uint8_t>(m));
}

// id -> name lookups, generated from the same lists - handy for telemetry and debug prints

/** @brief command name for @p id, or "UNKNOWN" if out of range */
inline const char* command_name(uint8_t id) {
    static const char* const names[] = {
#define FSW_COMMAND_X(name) #name,
        FSW_COMMAND_LIST(FSW_COMMAND_X)
#undef FSW_COMMAND_X
    };
    return id < sizeof(names) / sizeof(names[0]) ? names[id] : "UNKNOWN";
}

/** @brief fault name for @p id, or "UNKNOWN" if out of range */
inline const char* fault_name(uint8_t id) {
    static const char* const names[] = {
#define FSW_FAULT_X(name) #name,
        FSW_FAULT_LIST(FSW_FAULT_X)
#undef FSW_FAULT_X
    };
    return id < sizeof(names) / sizeof(names[0]) ? names[id] : "UNKNOWN";
}

/** @brief mode name for @p id, or "UNKNOWN" if out of range */
inline const char* mode_name(uint8_t id) {
    static const char* const names[] = {
#define FSW_MODE_X(name) #name,
        FSW_MODE_LIST(FSW_MODE_X)
#undef FSW_MODE_X
    };
    return id < sizeof(names) / sizeof(names[0]) ? names[id] : "UNKNOWN";
}

}  // namespace fsw

#endif  // COMMON_PROTOCOL_STATE_HPP
