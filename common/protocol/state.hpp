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
#define FSW_COMMAND_LIST(X)                                                        \
    X(NOOP)              /* link keep-alive / test */                              \
    X(SET_MODE)          /* request a mode transition (arg = mode id) */           \
    X(CLEAR_FAULT)       /* clear a latched fault (arg = fault id) */              \
    X(CAPTURE_IMAGE)     /* take a photo (arg = resolution id) */                  \
    X(SET_HEADING)       /* aim POINTING at a bearing (arg = binary angle) */      \
    X(REQUEST_TELEMETRY) /* beacon one frame of a telemetry kind (arg = msg id) */ \
    X(PULSE_WHEEL)       /* one bounded actuator pulse (arg = torque, signed mN m) */

// SET_HEADING's argument is a binary angle: one byte spanning a full turn, so a step is 360/256 =
// 1.406 degrees. a byte cannot hold degrees and this is the standard way round it - the same
// encoding flight software has used for bearings since long before anyone had a spare byte
inline constexpr float kHeadingStepRad = 6.283185307F / 256.0F;

// PULSE_WHEEL's argument is a signed byte of milli-newton-metres, and the pulse is commanded back
// to zero after this long. it is an engineering command: it drives the actuator directly, past
// the control laws, so that the plant can be characterised rather than inferred.
//
// it exists because the platform is only free when nothing is plugged into it. a laptop on the end
// of a USB cable is a torsion spring across the rotating joint - the bench proved that by watching
// a "friction" measurement spring back to where it started every time - so any measurement of the
// bearing has to be commanded over the radio and run by the vehicle itself
inline constexpr uint32_t kWheelPulseMs = 500;

// image sizes CAPTURE_IMAGE can ask for - X(name, label). the order here is the wire value of the
// command's argument, so appending is safe and reordering is not
#define FSW_RESOLUTION_LIST(X) \
    X(R320x240, "320x240")     \
    X(R800x600, "800x600")     \
    X(R1600x1200, "1600x1200")

// fault ids - X(name). a fault's order here is its id and its bit in heartbeat_t.faults
#define FSW_FAULT_LIST(X)                                                    \
    X(COMMAND_LINK_LOSS)  /* command-loss timer expired */                   \
    X(ACCEL_GYRO_DROPOUT) /* accel/gyro invalid or frozen */                 \
    X(MAG_DROPOUT)        /* magnetometer invalid or frozen */               \
    X(POWER_DROPOUT)      /* ina228 power monitor invalid/missing */         \
    X(UNDERVOLTAGE)       /* bus below the brownout threshold */             \
    X(OVERVOLTAGE)        /* bus above its max */                            \
    X(OVERCURRENT)        /* current draw over the limit */                  \
    X(TEMP_DROPOUT)       /* tmp117 temperature invalid/missing */           \
    X(UNDERTEMPERATURE)   /* below the min operating temp */                 \
    X(OVERTEMPERATURE)    /* above the max operating temp */                 \
    X(WHEEL_DROPOUT)      /* reaction wheel stopped answering on its link */ \
    X(CAMERA_DROPOUT)     /* payload camera not answering */                 \
    X(WHEEL_SATURATED)    /* wheel at its speed limit - no torque left to give */

// reset causes - X(name). order here is the id carried in boot_info_t.reset_cause (REQ-WDG-002).
// this is the wire's catalog, not the mcu's: the stm32 driver has its own reset_cause_t reading
// RCC_CSR, and the control task static_asserts the two agree rather than trusting them to.
//
// note WATCHDOG rather than IWDG, and WINDOW_WATCHDOG rather than WWDG: those two are CMSIS
// peripheral macros (`#define IWDG ((IWDG_TypeDef *) IWDG_BASE)`), and this header is compiled in
// the same translation unit as stm32f446xx.h. an entry here that collides with a peripheral macro
// expands into a pointer cast and buries the file in errors nowhere near the real cause. any name
// added to any catalog in this file has to survive that
#define FSW_RESET_CAUSE_LIST(X)                                                 \
    X(UNKNOWN)         /* no flag set, or a cause this build does not decode */ \
    X(POWER_ON)        /* cold power-up */                                      \
    X(RESET_PIN)       /* reset pin pulled low */                               \
    X(BROWNOUT)        /* supply dipped below the detect threshold */           \
    X(SOFTWARE)        /* a deliberate reset request from the firmware */       \
    X(WATCHDOG)        /* independent watchdog timed out - REQ-WDG-001 */       \
    X(WINDOW_WATCHDOG) /* window watchdog */                                    \
    X(LOWPOWER)        /* illegal low-power state */

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

enum class ImageResolution : uint8_t {
#define FSW_RESOLUTION_X(name, label) name,
    FSW_RESOLUTION_LIST(FSW_RESOLUTION_X)
#undef FSW_RESOLUTION_X
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

enum class ResetCause : uint8_t {
#define FSW_RESET_CAUSE_X(name) name,
    FSW_RESET_CAUSE_LIST(FSW_RESET_CAUSE_X)
#undef FSW_RESET_CAUSE_X
};

// catalog sizes - one entry per list, counted at compile time
inline constexpr uint8_t kCommandCount =
#define FSW_COMMAND_X(name) +1
    FSW_COMMAND_LIST(FSW_COMMAND_X);
#undef FSW_COMMAND_X

inline constexpr uint8_t kResolutionCount =
#define FSW_RESOLUTION_X(name, label) +1
    FSW_RESOLUTION_LIST(FSW_RESOLUTION_X);
#undef FSW_RESOLUTION_X

inline constexpr uint8_t kFaultCount =
#define FSW_FAULT_X(name) +1
    FSW_FAULT_LIST(FSW_FAULT_X);
#undef FSW_FAULT_X

inline constexpr uint8_t kModeCount =
#define FSW_MODE_X(name) +1
    FSW_MODE_LIST(FSW_MODE_X);
#undef FSW_MODE_X

inline constexpr uint8_t kResetCauseCount =
#define FSW_RESET_CAUSE_X(name) +1
    FSW_RESET_CAUSE_LIST(FSW_RESET_CAUSE_X);
#undef FSW_RESET_CAUSE_X

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

/** @brief image size label for @p id, or "UNKNOWN" if out of range */
inline const char* resolution_name(uint8_t id) {
    static const char* const names[] = {
#define FSW_RESOLUTION_X(name, label) label,
        FSW_RESOLUTION_LIST(FSW_RESOLUTION_X)
#undef FSW_RESOLUTION_X
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

/** @brief reset-cause name for @p id, or "UNKNOWN" if out of range */
inline const char* reset_cause_name(uint8_t id) {
    static const char* const names[] = {
#define FSW_RESET_CAUSE_X(name) #name,
        FSW_RESET_CAUSE_LIST(FSW_RESET_CAUSE_X)
#undef FSW_RESET_CAUSE_X
    };
    return id < sizeof(names) / sizeof(names[0]) ? names[id] : "UNKNOWN";
}

}  // namespace fsw

#endif  // COMMON_PROTOCOL_STATE_HPP
