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
 * @brief  hand a frame to the lora beacon, replacing any frame still waiting
 * @param  data  a complete encoded frame
 * @param  len   its length
 * @return true if it was accepted
 *
 * Replaces rather than queues, and that is the point: a beacon broadcasts the newest state, and
 * a heartbeat that missed its slot is worth nothing once a fresher one exists. Queueing them
 * would mean transmitting history at 57 ms a packet.
 *
 * The radio work happens in the telemetry task, never in the caller - rfm95_send takes the SPI3
 * bus lock, which the payload downlink can be holding, and the control task must not block on it.
 */
bool telemetry_out_beacon(const uint8_t* data, size_t len);

/**
 * @brief  bytes that would fit on *both* links right now
 * @return the smaller of the two links' free space
 *
 * For a producer that can choose to wait rather than be dropped - the payload downlink uses it to
 * pace itself against the wire instead of against a chunks-per-cycle constant.
 */
size_t telemetry_out_room(void);

/**
 * @brief  beacon frames replaced before they reached the air
 * @return the count
 *
 * Not a buffer overflow - the beacon is one slot deep by design, so a newer heartbeat displacing
 * an older one is normal at any rate above what the radio can carry. It is reported because the
 * count rising fast means the beacon is asking more of the link than it can give.
 */
uint32_t telemetry_out_beacon_dropped(void);

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
