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

    /**
     * @brief  evaluate imu data and update necessary faults
     * @param  imu    imu data sample
     * @param  fm     fault manager to feed
     * @param  t_ms   platform time
     */
    void evaluate_imu(const std::optional<imu_data_t>& imu, FaultManager& fm, uint32_t t_ms);

    /**
     * @brief  evaluate power data and update necessary faults
     * @param  power  power data sample
     * @param  fm     fault manager to feed
     * @param  t_ms   platform time
     */
    void evaluate_power(const std::optional<power_data_t>& power, FaultManager& fm, uint32_t t_ms);

    /**
     * @brief  evaluate temperature data and update necessary faults
     * @param  temp   temperature data sample
     * @param  fm     fault manager to feed
     * @param  t_ms   platform time
     */
    void evaluate_temp(const std::optional<temp_data_t>& temp, FaultManager& fm, uint32_t t_ms);

    /**
     * @brief  judge the wheel link's silence and update WHEEL_DROPOUT
     * @param  wheel  wheel status, set only on the cycles one arrived
     * @param  fm     fault manager to feed
     * @param  t_ms   platform time
     */
    void evaluate_wheel(const std::optional<wheel_status_t>& wheel, FaultManager& fm,
                        uint32_t t_ms);

    /**
     * @brief  evaluate payload camera health and update CAMERA_DROPOUT
     * @param  camera camera health sample
     * @param  fm     fault manager to feed
     * @param  t_ms   platform time
     */
    void evaluate_camera(const std::optional<camera_data_t>& camera, FaultManager& fm,
                         uint32_t t_ms);

    // last reading per source and when it last changed
    // imu
    int16_t prev_accel_[3] = {};
    int16_t prev_gyro_[3] = {};
    int16_t prev_mag_[3] = {};
    uint32_t accel_changed_ms_ = 0;
    uint32_t gyro_changed_ms_ = 0;
    uint32_t mag_changed_ms_ = 0;
    // wheel link - when the esc last answered, and whether it ever has (the first frame gets a
    // longer window, since the esc aligns foc before it can talk)
    uint32_t wheel_seen_ms_ = 0;
    bool wheel_acquired_ = false;
};

}  // namespace fsw

#endif  // FSW_SENSOR_MONITOR_HPP
