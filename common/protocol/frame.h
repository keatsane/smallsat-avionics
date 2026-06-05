/**
 * @file   frame.h
 * @brief  byte-stream framing - sync/length/crc envelope around a message
 */

#ifndef FRAME_H
#define FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRAME_SYNC0       0xAAU
#define FRAME_SYNC1       0x55U
#define FRAME_MAX_PAYLOAD 64U
#define FRAME_OVERHEAD    6U  // sync(2) + id(1) + len(1) + crc(2)
#define FRAME_MAX_SIZE    (FRAME_OVERHEAD + FRAME_MAX_PAYLOAD)

// a decoded, crc-checked frame
typedef struct {
    uint8_t msg_id;
    uint8_t len;
    uint8_t payload[FRAME_MAX_PAYLOAD];
} frame_t;

// incremental decoder state, zero-initialized before first use
typedef struct {
    uint8_t state;
    uint8_t msg_id;
    uint8_t len;
    uint8_t idx;
    uint8_t payload[FRAME_MAX_PAYLOAD];
    uint16_t crc_rx;
} frame_parser_t;

/**
 * @brief  wrap a message in a frame ready to transmit
 * @param  msg_id   message type id
 * @param  payload  payload bytes (may be NULL when len is 0)
 * @param  len      payload length, at most FRAME_MAX_PAYLOAD
 * @param  out      destination buffer, at least FRAME_MAX_SIZE bytes
 * @return number of bytes written to @p out, or 0 if len is too large
 */
size_t frame_encode(uint8_t msg_id, const uint8_t* payload, uint8_t len, uint8_t* out);

/**
 * @brief  feed one received byte into the frame decoder
 * @param  p     decoder state
 * @param  byte  the received byte
 * @param  out   filled in when a complete, valid frame is decoded
 * @return true when @p out holds a newly completed, crc-checked frame
 */
bool frame_decode(frame_parser_t* p, uint8_t byte, frame_t* out);

/**
 * @brief  crc-16/ccitt over a byte buffer
 * @param  data  bytes to checksum
 * @param  len   number of bytes
 * @return the 16-bit crc
 */
uint16_t crc16(const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif

#endif  // FRAME_H
