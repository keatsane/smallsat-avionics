/**
 * @file   frame.c
 * @brief  byte-stream framing - sync/length/crc envelope around a message
 */

#include "protocol/frame.h"

// decoder states
enum { F_SYNC0, F_SYNC1, F_ID, F_LEN, F_PAYLOAD, F_CRC_HI, F_CRC_LO };

size_t frame_encode(uint8_t msg_id, const uint8_t* payload, uint8_t len, uint8_t* out) {
    if (len > FRAME_MAX_PAYLOAD) {
        return 0;
    }
    size_t n = 0;
    out[n++] = FRAME_SYNC0;
    out[n++] = FRAME_SYNC1;
    out[n++] = msg_id;
    out[n++] = len;
    for (uint8_t i = 0; i < len; i++) {
        out[n++] = payload[i];
    }
    // crc covers id + len + payload (everything after the sync bytes)
    uint16_t crc = crc16(&out[2], (size_t)(2U + len));
    out[n++] = (uint8_t)(crc >> 8);
    out[n++] = (uint8_t)(crc & 0xFFU);
    return n;
}

bool frame_decode(frame_parser_t* p, uint8_t byte, frame_t* out) {
    switch (p->state) {
        case F_SYNC0:
            if (byte == FRAME_SYNC0) {
                p->state = F_SYNC1;
            }
            break;
        case F_SYNC1:
            p->state = (byte == FRAME_SYNC1) ? F_ID : F_SYNC0;
            break;
        case F_ID:
            p->msg_id = byte;
            p->state = F_LEN;
            break;
        case F_LEN:
            p->len = byte;
            p->idx = 0;
            if (byte > FRAME_MAX_PAYLOAD) {
                p->state = F_SYNC0;  // bad length, resync
            } else {
                p->state = (byte == 0) ? F_CRC_HI : F_PAYLOAD;
            }
            break;
        case F_PAYLOAD:
            p->payload[p->idx++] = byte;
            if (p->idx >= p->len) {
                p->state = F_CRC_HI;
            }
            break;
        case F_CRC_HI:
            p->crc_rx = (uint16_t)byte << 8;
            p->state = F_CRC_LO;
            break;
        case F_CRC_LO: {
            p->crc_rx |= byte;
            p->state = F_SYNC0;
            uint8_t tmp[2U + FRAME_MAX_PAYLOAD];
            tmp[0] = p->msg_id;
            tmp[1] = p->len;
            for (uint8_t i = 0; i < p->len; i++) {
                tmp[2U + i] = p->payload[i];
            }
            if (crc16(tmp, (size_t)(2U + p->len)) == p->crc_rx) {
                out->msg_id = p->msg_id;
                out->len = p->len;
                for (uint8_t i = 0; i < p->len; i++) {
                    out->payload[i] = p->payload[i];
                }
                return true;
            }
            break;
        }
        default:
            p->state = F_SYNC0;
            break;
    }
    return false;
}

uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}
