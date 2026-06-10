"""Tests for the frame/message codec (frames.py)."""

import struct

from frames import (
    MSG_COMMAND,
    MSG_COMMAND_ACK,
    MSG_HEARTBEAT,
    MSG_UART_STATUS,
    FrameDecoder,
    crc16,
    encode,
    format_frame,
)


def _decode_all(data: bytes):
    """Feed bytes through a fresh decoder, return the last completed frame or None."""
    decoder = FrameDecoder()
    result = None
    for byte in data:
        out = decoder.push(byte)
        if out is not None:
            result = out
    return result


def test_crc16_known_vector():
    # canonical CRC-16/CCITT-FALSE check value - pins the algorithm to the firmware
    assert crc16(b"123456789") == 0x29B1


def test_command_roundtrip():
    payload = struct.pack("<BBH", 1, 3, 99)
    assert _decode_all(encode(MSG_COMMAND, payload)) == (MSG_COMMAND, payload)


def test_heartbeat_roundtrip():
    payload = struct.pack("<IBIH", 123456, 1, 0x04, 42)
    assert _decode_all(encode(MSG_HEARTBEAT, payload)) == (MSG_HEARTBEAT, payload)


def test_uart_status_roundtrip():
    payload = struct.pack("<IIII", 3, 0, 1, 0)
    assert _decode_all(encode(MSG_UART_STATUS, payload)) == (MSG_UART_STATUS, payload)


def test_bad_crc_is_rejected():
    frame = bytearray(encode(MSG_HEARTBEAT, b"\x00" * 8))
    frame[-1] ^= 0xFF  # corrupt the low crc byte
    assert _decode_all(bytes(frame)) is None


def test_resync_after_garbage():
    stream = b"\x11\xaa\x22" + encode(MSG_UART_STATUS, b"\x00" * 16)
    assert _decode_all(stream) == (MSG_UART_STATUS, b"\x00" * 16)


def test_format_heartbeat():
    text = format_frame(MSG_HEARTBEAT, struct.pack("<IBIH", 5000, 0, 0, 5))
    assert "HEARTBEAT" in text
    assert "seq=5" in text


def test_format_command_ack():
    text = format_frame(MSG_COMMAND_ACK, struct.pack("<BHBB", 1, 7, 0, 2))
    assert "COMMAND_ACK" in text
    assert "rejected" in text
