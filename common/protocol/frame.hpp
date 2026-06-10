/**
 * @file   frame.hpp
 * @brief  byte-stream framing - sync/length/crc envelope around a message
 */

#ifndef COMMON_PROTOCOL_FRAME_HPP
#define COMMON_PROTOCOL_FRAME_HPP

#include <cstddef>
#include <cstdint>
#include <optional>

namespace fsw {

inline constexpr uint8_t kFrameSync0 = 0xAA;     // first sync byte
inline constexpr uint8_t kFrameSync1 = 0x55;     // second sync byte
inline constexpr uint8_t kFrameMaxPayload = 64;  // largest payload we frame
inline constexpr size_t kFrameOverhead = 6;      // sync(2) + id(1) + len(1) + crc(2)
inline constexpr size_t kFrameMaxSize = kFrameOverhead + kFrameMaxPayload;  // largest full frame

// where frame_decode is in the sync/id/len/payload/crc sequence
enum class FrameState : uint8_t {
    Sync0,    // waiting for the first sync byte
    Sync1,    // got sync0, waiting for the second sync byte
    Id,       // reading the message id
    Len,      // reading the payload length
    Payload,  // accumulating payload bytes
    CrcHi,    // reading the high crc byte
    CrcLo,    // reading the low crc byte, then validate the frame
};

// a decoded, crc-checked frame
struct frame_t {
    uint8_t msg_id;                     // message type id (MsgId in msg.hpp)
    uint8_t len;                        // payload length in bytes
    uint8_t payload[kFrameMaxPayload];  // the decoded payload
};

// incremental decoder state, zero-initialized before first use (state starts at FrameState::Sync0)
struct frame_parser_t {
    FrameState state;                   // current position in the decode state machine
    uint8_t msg_id;                     // message id captured this frame
    uint8_t len;                        // payload length captured this frame
    uint8_t idx;                        // payload bytes received so far
    uint8_t payload[kFrameMaxPayload];  // payload accumulated so far
    uint16_t crc_rx;                    // crc read off the wire
};

/**
 * @brief  wrap a message in a frame ready to transmit
 * @param  msg_id   message type id
 * @param  payload  payload bytes (may be NULL when len is 0)
 * @param  len      payload length, at most kFrameMaxPayload
 * @param  out      destination buffer, at least kFrameMaxSize bytes
 * @return number of bytes written to @p out, or 0 if len is too large
 */
size_t frame_encode(uint8_t msg_id, const uint8_t* payload, uint8_t len, uint8_t* out);

/**
 * @brief  feed one received byte into the frame decoder
 * @param  p     decoder state
 * @param  byte  the received byte
 * @return the decoded frame when this byte completes a valid one, else nullopt
 */
std::optional<frame_t> frame_decode(frame_parser_t* p, uint8_t byte);

/**
 * @brief  crc-16/ccitt over a byte buffer
 * @param  data  bytes to checksum
 * @param  len   number of bytes
 * @return the 16-bit crc
 */
uint16_t crc16(const uint8_t* data, size_t len);

}  // namespace fsw

#endif  // COMMON_PROTOCOL_FRAME_HPP
