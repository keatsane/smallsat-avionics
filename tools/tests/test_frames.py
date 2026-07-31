"""Tests for the frame/message codec (ground/frames.py)."""

import re
import struct
from pathlib import Path

import pytest

from ground import frames
from ground.frames import (
    COMMANDS,
    FAULTS,
    MODES,
    MSG_COMMAND,
    MSG_COMMAND_ACK,
    MSG_HEARTBEAT,
    MSG_IMU_DATA,
    MSG_POWER_DATA,
    MSG_TEMP_DATA,
    MSG_UART_STATUS,
    RESET_CAUSES,
    REJECT_REASONS,
    FrameDecoder,
    crc16,
    decode_boot_info,
    decode_radio_status,
    decode_camera_data,
    decode_command_ack,
    decode_task_health,
    decode_heartbeat,
    decode_imu_data,
    decode_power_data,
    decode_temp_data,
    encode,
    encode_command,
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


def test_command_ack_roundtrip():
    payload = struct.pack("<BH2B", 1, 7, 0, 2)
    assert _decode_all(encode(MSG_COMMAND_ACK, payload)) == (MSG_COMMAND_ACK, payload)


def test_format_command():
    text = format_frame(MSG_COMMAND, struct.pack("<BBH", 1, 3, 99))
    assert "COMMAND" in text
    assert "cmd=SET_MODE" in text  # named, not a raw id
    assert "seq=99" in text


def test_encode_command_roundtrip():
    # what command.py puts on the wire must decode back to the same command_t fields
    frame = encode_command(3, 0, 7)  # CAPTURE_IMAGE
    assert _decode_all(frame) == (MSG_COMMAND, struct.pack("<BBH", 3, 0, 7))


def test_heartbeat_roundtrip():
    payload = struct.pack("<IBIIHH", 123456, 1, 0x04, 0, 42, 0)
    assert _decode_all(encode(MSG_HEARTBEAT, payload)) == (MSG_HEARTBEAT, payload)


def test_uart_status_roundtrip():
    payload = struct.pack("<IIII", 3, 0, 1, 0)
    assert _decode_all(encode(MSG_UART_STATUS, payload)) == (MSG_UART_STATUS, payload)


def test_format_uart_status():
    text = format_frame(MSG_UART_STATUS, struct.pack("<IIII", 3, 0, 1, 0))
    assert "UART" in text
    assert "overrun=3" in text


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
    text = format_frame(MSG_HEARTBEAT, struct.pack("<IBIIHH", 5000, 0, 0, 0, 5, 14800))
    assert "bus=14.80V" in text
    assert "HEARTBEAT" in text
    assert "seq=5" in text


def test_format_command_ack():
    text = format_frame(MSG_COMMAND_ACK, struct.pack("<BH2B", 1, 7, 0, 2))
    assert "COMMAND_ACK" in text
    assert "rejected" in text


def test_decode_heartbeat_fields():
    payload = struct.pack("<IBIIHH", 123456, 5, 0x400, 0, 42, 16750)
    hb = decode_heartbeat(payload)
    assert hb == {
        "uptime_ms": 123456,
        "inhibited": 0,
        "mode": "SAFE",
        "faults": 0x400,
        "seq": 42,
        "bus_mv": 16750,
    }


def test_mode_name_out_of_range():
    assert mode_name(200) == "UNKNOWN"


def test_fault_names_decodes_bits():
    assert fault_names(0) == "{}"
    # bit 0 (COMMAND_LINK_LOSS) + bit 4 (UNDERVOLTAGE) = 0x11
    assert fault_names(0x11) == "{COMMAND_LINK_LOSS, UNDERVOLTAGE}"


def test_fault_names_unknown_bit():
    assert fault_names(1 << 20) == "{bit20}"


def test_format_heartbeat_decodes_faults():
    text = format_frame(MSG_HEARTBEAT, struct.pack("<IBIIHH", 5000, 5, 0x11, 0, 4, 0))
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
    assert "IMU" in text
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
    assert "POWER" in text
    assert "bus_mv=1" in text
    assert "current_ma=2" in text
    assert "power_mw=3" in text
    assert "flags=0x01" in text


def test_temp_data_roundtrip():
    # temp_mc is signed - exercise a below-zero reading
    payload = struct.pack("<IiB", 50, -5000, 0x01)
    assert _decode_all(encode(MSG_TEMP_DATA, payload)) == (MSG_TEMP_DATA, payload)


def test_decode_temp_data_fields():
    # temp_mc must surface negatives, not wrap to unsigned
    payload = struct.pack("<IiB", 50, -5000, 0x01)
    assert decode_temp_data(payload) == {
        "t_ms": 50,
        "temp_mc": -5000,
        "flags": 0x01,
    }


def test_format_temp_data():
    text = format_frame(MSG_TEMP_DATA, struct.pack("<IiB", 5000, 24187, 0x01))
    assert "TEMP" in text
    assert "temp_mc=24187" in text
    assert "flags=0x01" in text


def _task_health_payload(entries: list, flags: int = frames.TASK_FLAG_WATCHDOG_FED) -> bytes:
    """Pack a task_health_t: t_ms, count, flags, then every slot, unused ones left zero."""
    padded = entries + [(0, 0, 0, 0)] * (frames.TASK_HEALTH_MAX - len(entries))
    body = b"".join(struct.pack("<BBHH", *e) for e in padded)
    return struct.pack("<IBB", 12000, len(entries), flags) + body


def test_task_health_roundtrip():
    payload = _task_health_payload([(0, 2, 812, 100), (1, 2, 402, 95)])
    assert _decode_all(encode(frames.MSG_TASK_HEALTH, payload)) == (
        frames.MSG_TASK_HEALTH,
        payload,
    )


def test_decode_task_health_trims_to_count():
    # every slot is on the wire; only the first `count` of them mean anything
    payload = _task_health_payload([(0, 2, 812, 100), (1, 2, 402, 95)])
    d = decode_task_health(payload)
    assert d["t_ms"] == 12000
    assert d["count"] == 2
    assert [t["name"] for t in d["tasks"]] == ["control", "sensors"]
    assert d["tasks"][0] == {
        "id": 0,
        "name": "control",
        "state": 2,
        "stack_free_words": 812,
        "checkin_age_ms": 100,
    }


def test_decode_task_health_rejects_short_payload():
    # a truncated report must raise, never decode the entries it happens to have
    with pytest.raises(struct.error):
        decode_task_health(_task_health_payload([(0, 2, 812, 100)])[:-1])


def test_task_health_names_unknown_id():
    # a report from a newer build naming a slot this ground station does not know
    d = decode_task_health(_task_health_payload([(99, 1, 64, 0)]))
    assert d["tasks"][0]["name"] == "id99"


def test_format_task_health():
    # idle never checks in, so its age is the sentinel and must be left off entirely
    payload = _task_health_payload([(0, 2, 812, 100), (6, 1, 96, frames.TASK_CHECKIN_NONE)])
    text = format_frame(frames.MSG_TASK_HEALTH, payload)
    assert "TASKS" in text
    assert "control: blk 812w 100ms" in text
    assert "idle: rdy 96w" in text
    assert "65535" not in text  # the sentinel must never reach the screen as a number
    assert "WATCHDOG" not in text  # a fed watchdog is the routine case and stays quiet


def test_format_task_health_flags_an_unfed_watchdog():
    # the one state that must never be missed: a task is past its deadline, so the health task
    # withheld the pet and the board is about to reset itself
    payload = _task_health_payload([(0, 2, 812, 900)], flags=0)
    assert "WATCHDOG UNFED" in format_frame(frames.MSG_TASK_HEALTH, payload)


def test_downlink_status_renders_a_progress_bar():
    payload = struct.pack("<IHHHIH", 5000, 11, 30, 120, 90, 3)
    line = format_frame(frames.MSG_DOWNLINK_STATUS, payload)
    assert line.startswith("DOWNLINK")
    assert "30/120 (25%)" in line
    assert "dropped=3" in line
    # the bar has to move, or it is decoration rather than a progress indicator
    quarter = format_frame(frames.MSG_DOWNLINK_STATUS, payload)
    whole = format_frame(
        frames.MSG_DOWNLINK_STATUS, struct.pack("<IHHHIH", 5000, 11, 120, 120, 360, 3)
    )
    assert quarter.count("#") < whole.count("#")


def test_an_idle_downlink_reads_as_idle_not_as_a_full_bar():
    # nothing in flight is chunks=0. a bar drawn for that sits at 100% for as long as the vehicle
    # stays in DOWNLINK, which reads as a stall
    line = format_frame(frames.MSG_DOWNLINK_STATUS, struct.pack("<IHHHIH", 1, 7, 0, 0, 900, 5))
    assert "idle" in line
    assert "#" not in line
    assert "sent=900" in line


def test_ground_status_reports_a_dead_receiver():
    # the state that hid a broken payload link for three sessions: lora fine, nrf24 down
    payload = struct.pack("<IIIIBB", 9000, 42, 0, 0, 0, frames.GROUND_FLAG_LORA_UP)
    line = format_frame(frames.MSG_GROUND_STATUS, payload)
    assert line.startswith("GROUND")
    assert "lora up" in line
    assert "nrf24 DOWN" in line


def test_ground_status_reports_channel_occupancy():
    # a percentage rather than a flag, because a flag read "yes" forever: 2476 MHz has wifi and
    # bluetooth in it, and the detector trips on any of it
    up = frames.GROUND_FLAG_LORA_UP | frames.GROUND_FLAG_NRF24_UP
    assert "ch 3% busy" in format_frame(
        frames.MSG_GROUND_STATUS, struct.pack("<IIIIBB", 1, 5, 0, 0, 3, up)
    )
    assert "ch 94% busy" in format_frame(
        frames.MSG_GROUND_STATUS, struct.pack("<IIIIBB", 1, 5, 0, 0, 94, up)
    )


def test_ground_status_names_the_panel_addresses():
    # which address is missing is the whole question when a second panel does not light, so the
    # line names them rather than counting them
    both = frames.GROUND_FLAG_PANEL1 | frames.GROUND_FLAG_PANEL2
    assert "oled 3C+3D" in format_frame(
        frames.MSG_GROUND_STATUS, struct.pack("<IIIIBB", 1, 0, 0, 0, 0, both)
    )
    assert "oled 3C" in format_frame(
        frames.MSG_GROUND_STATUS, struct.pack("<IIIIBB", 1, 0, 0, 0, 0, frames.GROUND_FLAG_PANEL1)
    )
    assert "oled none" in format_frame(
        frames.MSG_GROUND_STATUS, struct.pack("<IIIIBB", 1, 0, 0, 0, 0, 0)
    )


def test_ground_status_len_matches_msg_hpp():
    header = REPO_ROOT / "common" / "protocol" / "msg.hpp"
    match = re.search(r"sizeof\(ground_status_t\)\s*==\s*(\d+)", header.read_text())
    assert match is not None
    assert struct.calcsize("<IIIIBB") == int(match.group(1))


def test_downlink_status_len_matches_msg_hpp():
    header = REPO_ROOT / "common" / "protocol" / "msg.hpp"
    match = re.search(r"sizeof\(downlink_status_t\)\s*==\s*(\d+)", header.read_text())
    assert match is not None
    assert struct.calcsize("<IHHHIH") == int(match.group(1))


def test_chunk_request_round_trips_and_pads():
    raw = frames.encode_chunk_request(7, [3, 19, 200])
    out = _decode_all(raw)
    assert out is not None
    msg_id, payload = out
    assert msg_id == frames.MSG_CHUNK_REQUEST
    image_id, count = struct.unpack_from("<HB", payload)
    chunks = struct.unpack_from(f"<{frames.CHUNK_REQUEST_MAX}H", payload, 3)
    assert (image_id, count) == (7, 3)
    assert chunks[:3] == (3, 19, 200)
    assert all(c == 0 for c in chunks[3:])  # unused entries are zero on the wire


def test_chunk_request_len_matches_msg_hpp():
    header = REPO_ROOT / "common" / "protocol" / "msg.hpp"
    match = re.search(r"sizeof\(chunk_request_t\)\s*==\s*(\d+)", header.read_text())
    assert match is not None
    assert 3 + 2 * frames.CHUNK_REQUEST_MAX == int(match.group(1))


def test_task_health_len_matches_msg_hpp():
    # the python length guard against the C++ sizeof assert - format_frame keys off it, so a
    # mismatch would silently render every report as UNKNOWN
    header = REPO_ROOT / "common" / "protocol" / "msg.hpp"
    match = re.search(r"sizeof\(task_health_t\)\s*==\s*(\d+)", header.read_text())
    assert match is not None
    assert frames.TASK_HEALTH_LEN == int(match.group(1))


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


def test_commands_mirror_state_hpp():
    header = REPO_ROOT / "common" / "protocol" / "state.hpp"
    assert COMMANDS == _xmacro_names(header, "FSW_COMMAND_LIST")


def test_modes_mirror_state_hpp():
    header = REPO_ROOT / "common" / "protocol" / "state.hpp"
    assert MODES == _xmacro_names(header, "FSW_MODE_LIST")


def test_resolutions_mirror_state_hpp():
    header = REPO_ROOT / "common" / "protocol" / "state.hpp"
    labels = re.findall(r'X\(\w+,\s*"([^"]+)"\)', header.read_text())
    assert labels == frames.RESOLUTIONS


def test_faults_mirror_state_hpp():
    header = REPO_ROOT / "common" / "protocol" / "state.hpp"
    assert FAULTS == _xmacro_names(header, "FSW_FAULT_LIST")


def test_reset_causes_mirror_state_hpp():
    header = REPO_ROOT / "common" / "protocol" / "state.hpp"
    assert RESET_CAUSES == _xmacro_names(header, "FSW_RESET_CAUSE_LIST")


def test_reject_reasons_mirror_command_handler_hpp():
    header = REPO_ROOT / "fsw" / "include" / "fsw" / "comms" / "command_handler.hpp"
    block = re.search(r"enum class CmdReject[^{]*\{(.*?)\}", header.read_text(), re.S)
    assert block is not None, "CmdReject enum not found"
    assert REJECT_REASONS == re.findall(r"^\s*(\w+),", block.group(1), re.M)


def _camel_to_macro(name: str) -> str:
    """MsgId CamelCase -> the MSG_SNAKE constant frames.py uses (ImuData -> MSG_IMU_DATA)."""
    return "MSG_" + "_".join(p.upper() for p in re.findall(r"[A-Z][a-z0-9]*", name))


def test_mode_ladder_mirrors_mode_manager_cpp():
    # the console prints this table as help, so a stale copy would tell an operator a transition
    # is legal when the flight software will refuse it
    from ground.commands import MODE_LADDER

    source = (REPO_ROOT / "fsw" / "src" / "mode_manager.cpp").read_text()
    # a row runs until the next row marker or the table's closing brace - rows wrap once a mode
    # has enough targets that clang-format splits the line
    rows = re.findall(r"/\* from (\w+)\s*\*/(.*?)(?=/\* from|\};)", source, re.S)
    assert rows, "kAutoAllowed table not found"

    # rows may name modes directly or use the kOperating clique alias; expand the alias from its
    # own definition, and drop the self-bit a clique row carries - is_legal refuses
    # self-transitions regardless of the table
    op_def = re.search(r"kOperating\s*=([^;]*);", source, re.S)
    operating = tuple(re.findall(r"Mode::(\w+)", op_def.group(1))) if op_def else ()

    def targets(mode: str, body: str) -> tuple:
        names = tuple(re.findall(r"Mode::(\w+)", body))
        if "kOperating" in body:
            names = tuple(m for m in operating if m != mode) + names
        return names

    expected = {mode: targets(mode, body) for mode, body in rows}
    # SAFE has no autonomous exit, so its row is a bare 0; the one way out is ground-commanded
    # (REQ-MODE-006), which is a carve-out in is_legal rather than a bit in the table
    assert expected["SAFE"] == ()
    expected["SAFE"] = ("STANDBY",)

    assert MODE_LADDER == expected


def test_msgids_mirror_msg_hpp():
    header = REPO_ROOT / "common" / "protocol" / "msg.hpp"
    block = re.search(r"enum class MsgId[^{]*\{(.*?)\}", header.read_text(), re.S)
    assert block is not None, "MsgId enum not found"
    # active entries only - reserved ids are commented out, so `name = 0xNN` won't match them
    pairs = re.findall(r"^\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+)", block.group(1), re.M)
    expected = {_camel_to_macro(name): int(val, 16) for name, val in pairs}
    actual = {k: v for k, v in vars(frames).items() if k.startswith("MSG_")}
    assert actual == expected


def test_payload_sizes_match_msg_hpp():
    # the mirrors above catch name drift; this catches wire-SIZE drift - a python decoder that
    # expects a different byte count than the C++ struct's sizeof assert in msg.hpp
    header = REPO_ROOT / "common" / "protocol" / "msg.hpp"
    sizes = {
        name: int(n)
        for name, n in re.findall(
            r"^\s*static_assert\(sizeof\((\w+)\)\s*==\s*(\d+)", header.read_text(), re.M
        )
    }
    # types with a decode_* helper - struct.unpack enforces the exact byte count
    decoders = {
        "command_ack_t": decode_command_ack,
        "heartbeat_t": decode_heartbeat,
        "imu_data_t": decode_imu_data,
        "power_data_t": decode_power_data,
        "temp_data_t": decode_temp_data,
        "camera_data_t": decode_camera_data,
        "task_health_t": decode_task_health,
        "boot_info_t": decode_boot_info,
        "radio_status_t": decode_radio_status,
    }
    for struct_name, decoder in decoders.items():
        n = sizes[struct_name]
        decoder(b"\x00" * n)  # the asserted size decodes cleanly - python and C++ agree
        with pytest.raises(struct.error):
            decoder(b"\x00" * (n - 1))  # a byte short must raise, never silently misread

    # command_t and uart_status_t are unpacked inline in format_frame; its len guard is the size
    for msg_id, struct_name in ((MSG_COMMAND, "command_t"), (MSG_UART_STATUS, "uart_status_t")):
        n = sizes[struct_name]
        assert not format_frame(msg_id, b"\x00" * n).startswith("UNKNOWN")  # right size -> decoded
        assert format_frame(msg_id, b"\x00" * (n - 1)).startswith(
            "UNKNOWN"
        )  # wrong size -> falls back
