/**
 * @file   telemetry.h
 * @brief  build and send periodic telemetry messages over the link
 */

#ifndef TELEMETRY_H
#define TELEMETRY_H

/**
 * @brief  send a heartbeat frame (uptime, mode, faults, sequence)
 */
void telemetry_send_heartbeat(void);

/**
 * @brief  send a link-status frame (uart receive-line error counts)
 */
void telemetry_send_link_status(void);

#endif  // TELEMETRY_H
