"""Tests for the frame/message codec (ground/frames.py)."""

import re
import struct
from pathlib import Path

from ground.frames import (
    MODES,
    MSG_COMMAND,
    MSG_COMMAND_ACK,
    MSG_HEARTBEAT,
    MSG_UART_STATUS,
    REJECT_REASONS,
    FrameDecoder,
    crc16,
    decode_heartbeat,
    encode,
    format_frame,
    mode_name,
)

REPO_ROOT = Path(__file__).resolve().parents[2]


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


def test_bad_crc_is_rejected_and_counted():
    frame = bytearray(encode(MSG_HEARTBEAT, b"\x00" * 8))
    frame[-1] ^= 0xFF  # corrupt the low crc byte
    decoder = FrameDecoder()
    assert all(decoder.push(b) is None for b in bytes(frame))
    assert decoder.crc_errors == 1


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


def test_decode_heartbeat_fields():
    payload = struct.pack("<IBIH", 123456, 5, 0x400, 42)
    hb = decode_heartbeat(payload)
    assert hb == {"uptime_ms": 123456, "mode": "SAFE", "faults": 0x400, "seq": 42}


def test_mode_name_out_of_range():
    assert mode_name(200) == "UNKNOWN"


# --- mirror drift tests ---
# the wire carries bare ints; the names live in c++. the python catalogs are hand-written
# mirrors, so each is checked against its owning header - add a name in c++ and forget
# python, and these go red naming exactly what drifted


def _xmacro_names(header: Path, macro: str) -> list:
    """Entry names of an X-macro list: the X(NAME) items in its #define block."""
    text = header.read_text()
    block = re.search(rf"#define {macro}\(X\)(.*?)\n\n", text, re.S)
    assert block is not None, f"{macro} not found in {header}"
    return re.findall(r"X\((\w+)\)", block.group(1))


def test_modes_mirror_state_hpp():
    header = REPO_ROOT / "common" / "protocol" / "state.hpp"
    assert MODES == _xmacro_names(header, "FSW_MODE_LIST")


def test_reject_reasons_mirror_command_handler_hpp():
    header = REPO_ROOT / "fsw" / "include" / "fsw" / "comms" / "command_handler.hpp"
    block = re.search(r"enum class CmdReject[^{]*\{(.*?)\}", header.read_text(), re.S)
    assert block is not None, "CmdReject enum not found"
    assert REJECT_REASONS == re.findall(r"^\s*(\w+),", block.group(1), re.M)
