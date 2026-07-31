/**
 * @file   downlink_task.hpp
 * @brief  empties the payload buffer to the ground while the vehicle is in DOWNLINK
 *
 * The flight software used to meter this itself, sending a fixed four chunks per control cycle.
 * That number was never a flight-software fact - it was the size of a uart tx ring divided by the
 * size of a frame, a link detail that had leaked across the platform boundary and would have been
 * wrong the moment the radio ran at a different rate.
 *
 * Now the executive says only *whether* to downlink and this task decides how fast, by filling the
 * output buffers and waiting for them to drain. The rate comes out as whatever the wire can carry.
 */

#ifndef DOWNLINK_TASK_HPP
#define DOWNLINK_TASK_HPP

/**
 * @brief  start or stop draining the payload buffer
 * @param  active  true while the vehicle is in DOWNLINK
 * Called every control cycle, so dropping out of DOWNLINK stops the stream within one cycle.
 */
void downlink_task_set_active(bool active);

/**
 * @brief  create the task
 * Call before the scheduler starts.
 */
void downlink_task_create(void);

#endif  // DOWNLINK_TASK_HPP
