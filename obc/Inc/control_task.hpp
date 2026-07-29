/**
 * @file   control_task.hpp
 * @brief  the fixed-rate flight-software cycle
 */

#ifndef CONTROL_TASK_HPP
#define CONTROL_TASK_HPP

/**
 * @brief  apply the bench fault inhibits, report them, and create the control task
 * call after the devices are initialized and before the scheduler starts
 * @param  camera_ok  whether the payload camera answered at init
 */
void control_task_create(bool camera_ok);

#endif  // CONTROL_TASK_HPP
