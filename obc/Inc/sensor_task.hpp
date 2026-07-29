/**
 * @file   sensor_task.hpp
 * @brief  the sensor task and the sample set it publishes
 */

#ifndef SENSOR_TASK_HPP
#define SENSOR_TASK_HPP

#include "protocol/msg.hpp"

// one cycle's worth of sensor state, already in flight-software form. the conversion from driver
// structs happens in the task, next to the read that produced it
struct sensor_set_t {
    fsw::imu_data_t imu;
    fsw::power_data_t power;
    fsw::temp_data_t temp;
};

/**
 * @brief  create the sensor task and the queue it publishes into
 * call after the devices are initialized and before the scheduler starts
 * @param  imu_ok    whether the imu answered at init
 * @param  power_ok  whether the power monitor answered at init
 * @param  temp_ok   whether the temperature sensor answered at init
 */
void sensor_task_create(bool imu_ok, bool power_ok, bool temp_ok);

/**
 * @brief  take the newest published sample set, if one has arrived since the last call
 * does not block - the control cycle's rate must not depend on the buses
 * @param  out  filled in only when true is returned
 * @return true if a set was waiting
 */
bool sensor_task_take(sensor_set_t* out);

#endif  // SENSOR_TASK_HPP
