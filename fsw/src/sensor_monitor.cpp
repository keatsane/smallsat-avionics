/**
 * @file   sensor_monitor.cpp
 * @brief  sensor-fault detection - invalid or frozen readings into dropout faults (REQ-SNS-002)
 */

#include "fsw/sensor_monitor.hpp"

namespace fsw {
namespace {

// a live sensor's noise moves the reading every sample, so unchanged for this long means frozen.
// imu-specific - a slow source (temperature) would want a much longer window
constexpr uint32_t kImuStaleWindowMs = 500;

// power thresholds - the ina228 sits high-side on the main 4s lipo bus, so bus voltage reads
// state of charge (16.8 V full, 14.8 V nominal)
constexpr uint32_t kBusUnderMv = 13600;      // 3.4 V/cell - low, but short of damage
constexpr uint32_t kBusOverMv = 17000;       // just over a full charge
constexpr int32_t kBusOverCurrentMa = 1500;  // ~200 mA logic + ~500 mA motor, under the ptc hold

// temperature limits, signed milli-degrees C (the sensor reads below zero). conservative avionics
// bounds - tunable; drop the over-limit to ~35000 to trip overtemperature by hand on the bench
constexpr int32_t kTempUnderMc = 0;     // 0 degC - freezing, out of the operating range
constexpr int32_t kTempOverMc = 60000;  // 60 degC - avionics running too hot

// the esc beacons its status every 250 ms, so four missed in a row is a dead link rather than
// one dropped frame. with the debounce of 3 that latches WHEEL_DROPOUT about 1.3 s after the
// last frame
constexpr uint32_t kWheelTimeoutMs = 1000;

// the esc spends several seconds aligning foc from cold and says nothing until it finishes
// (~5 s measured on the bench), so the first frame gets a longer window. without it every boot
// would latch a dropout that clears itself, which is how a fault gets trained into background
// noise. after the grace expires an esc that never came up latches like any other dead link
constexpr uint32_t kWheelAcquireMs = 8000;

// the wheel counts as full at 90% of its measured top speed (~36 rad/s on this rig, 2026-07-21).
// not 100%: the last few percent is where back-EMF has already flattened the torque curve, so a
// wheel that close is out of useful authority whatever the tachometer says
constexpr int32_t kWheelSaturatedMrads = 32000;

// true if cur has not differed from prev for longer than the stale window; remembers the latest
// reading and the time it last changed as a side effect
bool source_stale(const int16_t cur[3], int16_t prev[3], uint32_t& changed_ms, uint32_t t_ms) {
    if (cur[0] != prev[0] || cur[1] != prev[1] || cur[2] != prev[2]) {
        prev[0] = cur[0];
        prev[1] = cur[1];
        prev[2] = cur[2];
        changed_ms = t_ms;
    }
    return (t_ms - changed_ms) > kImuStaleWindowMs;
}

}  // namespace

void SensorMonitor::evaluate(const Inputs& inputs, FaultManager& fm, uint32_t t_ms) {
    evaluate_imu(inputs.imu, fm, t_ms);
    evaluate_power(inputs.power, fm, t_ms);
    evaluate_temp(inputs.temp, fm, t_ms);
    evaluate_wheel(inputs.wheel, fm, t_ms);
    evaluate_camera(inputs.camera, fm, t_ms);
}

void SensorMonitor::evaluate_imu(const std::optional<imu_data_t>& imu, FaultManager& fm,
                                 uint32_t t_ms) {
    if (!imu.has_value()) {
        return;  // no imu offered this cycle
    }

    const bool accel_gyro_ok = (imu->flags & kImuFlagAccelGyroValid) != 0U;
    const bool mag_ok = (imu->flags & kImuFlagMagValid) != 0U;

    // copy out of the packed wire struct before taking any addresses (unaligned pointer otherwise)
    const int16_t accel[3] = {imu->accel[0], imu->accel[1], imu->accel[2]};
    const int16_t gyro[3] = {imu->gyro[0], imu->gyro[1], imu->gyro[2]};
    const int16_t mag[3] = {imu->mag[0], imu->mag[1], imu->mag[2]};

    // staleness (frozen values) is only meaningful while the half is responding - a not-responding
    // half is the dropout case below, and its frozen 0xff would mislead the change tracker. accel
    // and gyro share the read but can freeze independently, so each source is tracked separately
    bool accel_frozen = false;
    bool gyro_frozen = false;
    bool mag_frozen = false;
    if (accel_gyro_ok) {
        accel_frozen = source_stale(accel, prev_accel_, accel_changed_ms_, t_ms);
        gyro_frozen = source_stale(gyro, prev_gyro_, gyro_changed_ms_, t_ms);
    }
    if (mag_ok) {
        mag_frozen = source_stale(mag, prev_mag_, mag_changed_ms_, t_ms);
    }
    const bool accel_gyro_stale = accel_frozen || gyro_frozen;

    // a half's data is unusable if it is not responding OR responding but frozen; either way it
    // raises that half's dropout (REQ-SNS-002)
    fm.update(Fault::ACCEL_GYRO_DROPOUT, !accel_gyro_ok || accel_gyro_stale, t_ms);
    fm.update(Fault::MAG_DROPOUT, !mag_ok || mag_frozen, t_ms);
}

void SensorMonitor::evaluate_power(const std::optional<power_data_t>& power, FaultManager& fm,
                                   uint32_t t_ms) {
    if (!power.has_value()) {
        return;  // no power sample this cycle
    }

    if ((power->flags & kPowerFlagValid) == 0U) {
        fm.update(Fault::POWER_DROPOUT, true, t_ms);
        return;
    }

    fm.update(Fault::POWER_DROPOUT, false, t_ms);

    // value-based faults straight off the sample
    fm.update(Fault::UNDERVOLTAGE, power->bus_mv < kBusUnderMv, t_ms);
    fm.update(Fault::OVERVOLTAGE, power->bus_mv > kBusOverMv, t_ms);
    fm.update(Fault::OVERCURRENT, power->current_ma > kBusOverCurrentMa, t_ms);
}

void SensorMonitor::evaluate_temp(const std::optional<temp_data_t>& temp, FaultManager& fm,
                                  uint32_t t_ms) {
    if (!temp.has_value()) {
        return;  // no temperature sample this cycle
    }

    if ((temp->flags & kTempFlagValid) == 0U) {
        fm.update(Fault::TEMP_DROPOUT, true, t_ms);
        return;
    }

    fm.update(Fault::TEMP_DROPOUT, false, t_ms);

    // value-based faults straight off the sample
    fm.update(Fault::UNDERTEMPERATURE, temp->temp_mc < kTempUnderMc, t_ms);
    fm.update(Fault::OVERTEMPERATURE, temp->temp_mc > kTempOverMc, t_ms);
}

void SensorMonitor::evaluate_wheel(const std::optional<wheel_status_t>& wheel, FaultManager& fm,
                                   uint32_t t_ms) {
    // the odd one out: the sensors are sampled every cycle, so for them a missing sample is no
    // opinion. the wheel talks on its own schedule, so silence is the whole measurement and an
    // arriving frame is only the thing that resets the clock. the flags it carries say whether
    // the wheel can be driven, which is a separate question from whether the link is up
    if (wheel.has_value()) {
        wheel_seen_ms_ = t_ms;
        wheel_acquired_ = true;
    }

    const uint32_t window = wheel_acquired_ ? kWheelTimeoutMs : kWheelAcquireMs;
    fm.update(Fault::WHEEL_DROPOUT, (t_ms - wheel_seen_ms_) > window, t_ms);

    // and whether it has anything left to give. a wheel at its speed limit cannot be accelerated
    // further, so the reaction it puts on the platform is zero however much torque is asked for -
    // the controller keeps commanding and the vehicle keeps not moving, which is exactly what the
    // bench showed: full torque held for fifteen seconds with the rate pinned at zero. only
    // reported while the link is up, because a stale reading is not evidence of anything
    if (wheel.has_value()) {
        const int32_t v = wheel->velocity_mrad_s;
        const int32_t mag = (v < 0) ? -v : v;
        fm.update(Fault::WHEEL_SATURATED, mag >= kWheelSaturatedMrads, t_ms);
    }
}

void SensorMonitor::evaluate_camera(const std::optional<camera_data_t>& camera, FaultManager& fm,
                                    uint32_t t_ms) {
    if (!camera.has_value()) {
        return;  // no camera reading this cycle
    }

    // back on the sampled-every-cycle pattern: the arducam's test register is polled on the same
    // cadence as the other devices, so validity is a property of the sample and not of silence
    fm.update(Fault::CAMERA_DROPOUT, (camera->flags & kCameraFlagValid) == 0U, t_ms);
}

}  // namespace fsw
