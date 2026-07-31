/**
 * @file   uplink_task.hpp
 * @brief  decodes ground commands off both uplinks and queues them for the control task
 */

#ifndef UPLINK_TASK_HPP
#define UPLINK_TASK_HPP

#include "protocol/msg.hpp"

/**
 * @brief  create the uplink task
 */
void uplink_task_create(void);

/**
 * @brief  take the next queued command, if one is waiting
 * @param  out  filled with the command when true is returned
 * @return true if a command was taken
 *
 * Never blocks - the control task calls this on its own schedule and one command enters each
 * cycle, because fsw::Inputs carries one. A burst arriving inside a single cycle now waits in the
 * queue instead of being overwritten, which is what the old drain-in-the-control-task path did.
 */
bool uplink_task_take(fsw::command_t* out);

#endif  // UPLINK_TASK_HPP
