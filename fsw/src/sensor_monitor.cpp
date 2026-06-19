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

// power thresholds - the ina228 watches the 5 V logic rail today (~150 mA avionics draw). planned:
// move it to the battery feed at phase 8 for state-of-charge, which shifts these to the lipo range
constexpr uint32_t kBusUnderMv = 4500;      // 4.5 V (-10%)
constexpr uint32_t kBusOverMv = 5500;       // 5.5 V (+10%)
constexpr int32_t kBusOverCurrentMa = 300;  // above the idle draw, tune to ~2x measured baseline

// temperature limits, signed milli-degrees C (the sensor reads below zero). conservative avionics
// bounds - tunable; drop the over-limit to ~35000 to trip overtemperature by hand on the bench
constexpr int32_t kTempUnderMc = 0;     // 0 degC - freezing, out of the operating range
constexpr int32_t kTempOverMc = 60000;  // 60 degC - avionics running too hot

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

}  // namespace fsw
