/**
 * @file   msg.h
 * @brief  message ids and payload layouts carried over the link
 */

#ifndef MSG_H
#define MSG_H

#include <stdint.h>

// message types that ride inside a frame
typedef enum {
    MSG_HEARTBEAT = 0x01,
    MSG_LINK_STATUS = 0x02,
} msg_id_t;

// MSG_HEARTBEAT - periodic "node is alive" beacon (little-endian on the wire)
typedef struct __attribute__((packed)) {
    uint32_t uptime_ms;  // millis() since boot
    uint8_t mode;        // current mode-manager state
    uint8_t faults;      // active fault flags
    uint16_t seq;        // increments each heartbeat, so drops are visible
} heartbeat_t;

// MSG_LINK_STATUS - uart receive-line error counts (little-endian on the wire)
typedef struct __attribute__((packed)) {
    uint32_t overrun;
    uint32_t framing;
    uint32_t noise;
    uint32_t dropped;
} link_status_t;

#endif  // MSG_H
