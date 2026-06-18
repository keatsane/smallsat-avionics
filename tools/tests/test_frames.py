"""Tests for the frame/message codec (ground/frames.py)."""

import re
import struct
from pathlib import Path

from ground import frames
from ground.frames import (
    FAULTS,
    MODES,
    MSG_COMMAND,
    MSG_COMMAND_ACK,
    MSG_HEARTBEAT,
    MSG_IMU_DATA,
    MSG_POWER_DATA,
    MSG_UART_STATUS,
    REJECT_REASONS,
    FrameDecoder,
    crc16,
    decode_heartbeat,
    decode_imu_data,
    decode_power_data,
    encode,
    fault_names,
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
    text = format_frame(MSG_COMMAND_ACK, struct.pack("<BH2B", 1, 7, 0, 2))
    assert "COMMAND_ACK" in text
    assert "rejected" in text


def test_decode_heartbeat_fields():
    payload = struct.pack("<IBIH", 123456, 5, 0x400, 42)
    hb = decode_heartbeat(payload)
    assert hb == {"uptime_ms": 123456, "mode": "SAFE", "faults": 0x400, "seq": 42}


def test_mode_name_out_of_range():
    assert mode_name(200) == "UNKNOWN"


def test_fault_names_decodes_bits():
    assert fault_names(0) == "{}"
    # bit 0 (COMMAND_LINK_LOSS) + bit 4 (UNDERVOLTAGE) = 0x11
    assert fault_names(0x11) == "{COMMAND_LINK_LOSS, UNDERVOLTAGE}"


def test_fault_names_unknown_bit():
    assert fault_names(1 << 20) == "{bit20}"


def test_format_heartbeat_decodes_faults():
    text = format_frame(MSG_HEARTBEAT, struct.pack("<IBIH", 5000, 5, 0x11, 4))
    assert "mode=SAFE" in text
    assert "faults={COMMAND_LINK_LOSS, UNDERVOLTAGE}" in text


def test_imu_data_roundtrip():
    payload = struct.pack("<I9hB", 1000, 16380, -8, 4, 1, -1, 0, 445, 190, 199, 0x03)
    assert _decode_all(encode(MSG_IMU_DATA, payload)) == (MSG_IMU_DATA, payload)


def test_decode_imu_data_fields():
    # all-0xff is the unplugged signature - the signed decode must surface -1, not 65535
    payload = struct.pack("<I9hB", 50, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0x00)
    assert decode_imu_data(payload) == {
        "t_ms": 50,
        "accel": (-1, -1, -1),
        "gyro": (-1, -1, -1),
        "mag": (-1, -1, -1),
        "flags": 0x00,
    }


def test_format_imu_data():
    text = format_frame(MSG_IMU_DATA, struct.pack("<I9hB", 5000, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0x03))
    assert "IMU_DATA" in text
    assert "accel=(1, 2, 3)" in text
    assert "flags=0x03" in text


def test_power_data_roundtrip():
    # bus_mv and power_mw are unsigned; current_ma is signed
    payload = struct.pack("<2IiIB", 50, 5000, -250, 1250, 0x01)
    assert _decode_all(encode(MSG_POWER_DATA, payload)) == (MSG_POWER_DATA, payload)


def test_decode_power_data_fields():
    # current_ma must surface negatives, not wrap to unsigned
    payload = struct.pack("<2IiIB", 50, 5000, -250, 1250, 0x01)
    assert decode_power_data(payload) == {
        "t_ms": 50,
        "bus_mv": 5000,
        "current_ma": -250,
        "power_mw": 1250,
        "flags": 0x01,
    }


def test_format_power_data():
    text = format_frame(MSG_POWER_DATA, struct.pack("<2IiIB", 5000, 1, 2, 3, 0x01))
    assert "POWER_DATA" in text
    assert "bus_mv=1" in text
    assert "current_ma=2" in text
    assert "power_mw=3" in text
    assert "flags=0x01" in text


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


def test_faults_mirror_state_hpp():
    header = REPO_ROOT / "common" / "protocol" / "state.hpp"
    assert FAULTS == _xmacro_names(header, "FSW_FAULT_LIST")


def test_reject_reasons_mirror_command_handler_hpp():
    header = REPO_ROOT / "fsw" / "include" / "fsw" / "comms" / "command_handler.hpp"
    block = re.search(r"enum class CmdReject[^{]*\{(.*?)\}", header.read_text(), re.S)
    assert block is not None, "CmdReject enum not found"
    assert REJECT_REASONS == re.findall(r"^\s*(\w+),", block.group(1), re.M)


def _camel_to_macro(name: str) -> str:
    """MsgId CamelCase -> the MSG_SNAKE constant frames.py uses (ImuData -> MSG_IMU_DATA)."""
    return "MSG_" + "_".join(p.upper() for p in re.findall(r"[A-Z][a-z0-9]*", name))


def test_msgids_mirror_msg_hpp():
    header = REPO_ROOT / "common" / "protocol" / "msg.hpp"
    block = re.search(r"enum class MsgId[^{]*\{(.*?)\}", header.read_text(), re.S)
    assert block is not None, "MsgId enum not found"
    # active entries only - reserved ids are commented out, so `name = 0xNN` won't match them
    pairs = re.findall(r"^\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+)", block.group(1), re.M)
    expected = {_camel_to_macro(name): int(val, 16) for name, val in pairs}
    actual = {k: v for k, v in vars(frames).items() if k.startswith("MSG_")}
    assert actual == expected
