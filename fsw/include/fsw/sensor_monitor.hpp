/**
 * @file   sensor_monitor.hpp
 * @brief  turns sensor readings into fault samples for the fault manager (the detect step)
 */

#ifndef FSW_SENSOR_MONITOR_HPP
#define FSW_SENSOR_MONITOR_HPP

#include <cstdint>
#include <optional>

#include "fsw/fault_manager.hpp"
#include "fsw/inputs.hpp"
#include "protocol/msg.hpp"

namespace fsw {

// converts raw sensor readings into per-fault good/bad samples and feeds the fault manager
class SensorMonitor {
   public:
    /**
     * @brief  judge all of this cycle's sensor readings and update their dropout faults
     * @param  inputs this cycle's inputs (the sensor samples it carries)
     * @param  fm     fault manager to feed (it debounces and latches)
     * @param  t_ms   platform time (ms since boot)
     */
    void evaluate(const Inputs& inputs, FaultManager& fm, uint32_t t_ms);

   private:
    // one detector per sensor, called by evaluate(); a nullopt sample is no opinion (REQ-SNS-002)
    void evaluate_imu(const std::optional<imu_data_t>& imu, FaultManager& fm, uint32_t t_ms);

    // staleness state: the last reading per source and when it last changed. a live sensor's noise
    // moves the reading every sample, so "unchanged for too long" means frozen (REQ-SNS-002).
    // accel and gyro share one read but can freeze independently, so each source is tracked
    int16_t prev_accel_[3] = {};
    int16_t prev_gyro_[3] = {};
    int16_t prev_mag_[3] = {};
    uint32_t accel_changed_ms_ = 0;
    uint32_t gyro_changed_ms_ = 0;
    uint32_t mag_changed_ms_ = 0;
};

}  // namespace fsw

#endif  // FSW_SENSOR_MONITOR_HPP
