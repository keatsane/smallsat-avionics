/**
 * @file   frame.cpp
 * @brief  byte-stream framing - sync/length/crc envelope around a message
 */

#include "protocol/frame.hpp"

namespace fsw {

size_t frame_encode(uint8_t msg_id, const uint8_t* payload, uint8_t len, uint8_t* out) {
    if (len > kFrameMaxPayload) {
        return 0;
    }
    size_t n = 0;
    out[n++] = kFrameSync0;
    out[n++] = kFrameSync1;
    out[n++] = msg_id;
    out[n++] = len;
    for (uint8_t i = 0; i < len; i++) {
        out[n++] = payload[i];
    }
    // crc covers id + len + payload (everything after the sync bytes)
    uint16_t crc = crc16(&out[2], static_cast<size_t>(2U + len));
    out[n++] = static_cast<uint8_t>(crc >> 8);
    out[n++] = static_cast<uint8_t>(crc & 0xFFU);
    return n;
}

std::optional<frame_t> frame_decode(frame_parser_t* p, uint8_t byte) {
    switch (p->state) {
        case FrameState::Sync0:
            if (byte == kFrameSync0) {
                p->state = FrameState::Sync1;
            }
            break;
        case FrameState::Sync1:
            p->state = (byte == kFrameSync1) ? FrameState::Id : FrameState::Sync0;
            break;
        case FrameState::Id:
            p->msg_id = byte;
            p->state = FrameState::Len;
            break;
        case FrameState::Len:
            p->len = byte;
            p->idx = 0;
            if (byte > kFrameMaxPayload) {
                p->state = FrameState::Sync0;  // bad length, resync
            } else {
                p->state = (byte == 0) ? FrameState::CrcHi : FrameState::Payload;
            }
            break;
        case FrameState::Payload:
            p->payload[p->idx++] = byte;
            if (p->idx >= p->len) {
                p->state = FrameState::CrcHi;
            }
            break;
        case FrameState::CrcHi:
            p->crc_rx = static_cast<uint16_t>(byte << 8);
            p->state = FrameState::CrcLo;
            break;
        case FrameState::CrcLo: {
            p->crc_rx |= byte;
            p->state = FrameState::Sync0;
            uint8_t tmp[2U + kFrameMaxPayload];
            tmp[0] = p->msg_id;
            tmp[1] = p->len;
            for (uint8_t i = 0; i < p->len; i++) {
                tmp[2U + i] = p->payload[i];
            }
            if (crc16(tmp, static_cast<size_t>(2U + p->len)) == p->crc_rx) {
                frame_t out;
                out.msg_id = p->msg_id;
                out.len = p->len;
                for (uint8_t i = 0; i < p->len; i++) {
                    out.payload[i] = p->payload[i];
                }
                return out;
            }
            break;
        }
        default:
            p->state = FrameState::Sync0;
            break;
    }
    return std::nullopt;
}

uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= static_cast<uint16_t>(data[i] << 8);
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000U) ? static_cast<uint16_t>((crc << 1) ^ 0x1021U)
                                  : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

}  // namespace fsw
