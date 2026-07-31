/**
 * @file   telemetry_task.hpp
 * @brief  the one task that writes to the console and downlink uarts
 *
 * Every other task hands bytes over here and returns immediately. That matters because uart_write
 * blocks when its 256-byte tx ring fills, and at 115200 a busy DOWNLINK cycle offers more than the
 * ring holds - so the blocking used to land in the control task, which is the highest priority in
 * the system and the one place a stall costs a late control response (REQ-RT-002).
 *
 * The blocking did not go away, it moved: this task sits below every producer, so while it waits
 * on the wire the rest of the system runs. That is the whole point of the handoff.
 *
 * It also makes `configUSE_NEWLIB_REENTRANT 0` honest. With one owner of console output, formatted
 * text has a single writer by construction rather than by convention.
 */

#ifndef TELEMETRY_TASK_HPP
#define TELEMETRY_TASK_HPP

#include <stddef.h>
#include <stdint.h>

/**
 * @brief  queue bytes for the console link (text and telemetry frames)
 * Never blocks. The write is all-or-nothing: a message that does not fit is dropped whole and
 * counted, because half a frame spliced into a byte stream leaves the ground resyncing on garbage.
 * @param  data  bytes to send
 * @param  len   how many
 * @return true if the whole message was queued, false if it was dropped
 */
bool telemetry_out_console(const uint8_t* data, size_t len);

/**
 * @brief  queue bytes for the downlink link (telemetry frames only)
 * @param  data  bytes to send
 * @param  len   how many
 * @return true if the whole message was queued, false if it was dropped
 */
bool telemetry_out_downlink(const uint8_t* data, size_t len);

/**
 * @brief  bytes that would fit on *both* links right now
 * @return the smaller of the two links' free space
 *
 * For a producer that can choose to wait rather than be dropped - the payload downlink uses it to
 * pace itself against the wire instead of against a chunks-per-cycle constant.
 */
size_t telemetry_out_room(void);

/**
 * @brief  messages dropped for want of buffer space since boot, both links together
 * @return the count
 */
uint32_t telemetry_out_dropped(void);

/**
 * @brief  create the output buffers and the task
 * Call before any task that produces output, and before the scheduler starts.
 */
void telemetry_task_create(void);

#endif  // TELEMETRY_TASK_HPP
