"""Frame and message codec for the avionics link.

The single Python implementation of the wire format defined in C++ under
common/protocol/ (frame.hpp, msg.hpp, state.hpp):
[AA 55][id][len][payload][crc16, big-endian].
Shared by the serial monitor and the host tests so the contract lives in one place.
"""

import struct

SYNC0 = 0xAA
SYNC1 = 0x55

# message ids - mirror MsgId in msg.hpp
MSG_COMMAND = 0x01
MSG_COMMAND_ACK = 0x02
MSG_HEARTBEAT = 0x03
MSG_UART_STATUS = 0x04
# LoraStatus = 0x05,
# Nrf24Status = 0x06,
MSG_IMU_DATA = 0x07
# PowerData = 0x08,
# TempData = 0x09,
# PayloadData = 0x10,


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) - matches frame.cpp."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode(msg_id: int, payload: bytes) -> bytes:
    """Wrap a message in a frame ready to send (mirror of frame_encode)."""
    body = bytes([msg_id, len(payload)]) + payload
    crc = crc16(body)
    return bytes([SYNC0, SYNC1]) + body + bytes([crc >> 8, crc & 0xFF])


class FrameDecoder:
    """Incremental frame decoder - feed it one byte at a time."""

    def __init__(self):
        self.reset()

    def reset(self):
        self.state = "sync0"
        self.msg_id = 0
        self.length = 0
        self.payload = bytearray()
        self.crc_rx = 0
        self.crc_errors = 0  # frames dropped on a bad crc since reset

    def push(self, byte: int):
        """Return (msg_id, payload) when a valid frame completes, else None."""
        if self.state == "sync0":
            if byte == SYNC0:
                self.state = "sync1"
        elif self.state == "sync1":
            self.state = "id" if byte == SYNC1 else "sync0"
        elif self.state == "id":
            self.msg_id = byte
            self.state = "len"
        elif self.state == "len":
            self.length = byte
            self.payload = bytearray()
            self.state = "crc_hi" if byte == 0 else "payload"
        elif self.state == "payload":
            self.payload.append(byte)
            if len(self.payload) >= self.length:
                self.state = "crc_hi"
        elif self.state == "crc_hi":
            self.crc_rx = byte << 8
            self.state = "crc_lo"
        elif self.state == "crc_lo":
            self.crc_rx |= byte
            self.state = "sync0"
            body = bytes([self.msg_id, self.length]) + bytes(self.payload)
            if crc16(body) == self.crc_rx:
                return self.msg_id, bytes(self.payload)
            self.crc_errors += 1
        return None


# rejection reasons - mirror CmdReject in fsw/include/fsw/comms/command_handler.hpp
REJECT_REASONS = ["Ok", "UnknownId", "IllegalInMode", "BadArg"]


def reject_name(reason: int) -> str:
    """Name for an ack's reason byte, or 'UNKNOWN' if out of range."""
    return REJECT_REASONS[reason] if 0 <= reason < len(REJECT_REASONS) else "UNKNOWN"


def decode_command_ack(payload: bytes) -> dict:
    """Unpack a command_ack_t payload (msg.hpp) into a dict."""
    cmd_id, seq, accepted, reason = struct.unpack("<BHBB", payload)
    return {
        "cmd_id": cmd_id,
        "seq": seq,
        "accepted": bool(accepted),
        "reason": reject_name(reason),
    }


# modes - mirror FSW_MODE_LIST in common/protocol/state.hpp (drift-checked by test_frames)
MODES = ["BOOT", "STANDBY", "DETUMBLE", "POINTING", "DOWNLINK", "SAFE"]


def mode_name(mode: int) -> str:
    """Name for a heartbeat's mode byte, or 'UNKNOWN' if out of range."""
    return MODES[mode] if 0 <= mode < len(MODES) else "UNKNOWN"


def decode_heartbeat(payload: bytes) -> dict:
    """Unpack a heartbeat_t payload (msg.hpp) into a dict."""
    uptime_ms, mode, faults, seq = struct.unpack("<IBIH", payload)
    return {
        "uptime_ms": uptime_ms,
        "mode": mode_name(mode),
        "faults": faults,
        "seq": seq,
    }


def decode_imu(payload: bytes) -> dict:
    """Unpack a imu_data_t payload (msg.hpp) into a dict."""
    t_ms, ax, ay, az, gx, gy, gz, mx, my, mz, flags = struct.unpack("<I9hB", payload)
    return {
        "t_ms": t_ms,
        "accel": (ax, ay, az),
        "gyro": (gx, gy, gz),
        "mag": (mx, my, mz),
        "flags": flags,
    }


def format_frame(msg_id: int, payload: bytes) -> str:
    """One-line human-readable summary of a decoded frame."""
    if msg_id == MSG_COMMAND and len(payload) == 4:
        cmd_id, arg, seq = struct.unpack("<BBH", payload)
        return f"COMMAND      cmd={cmd_id}  arg={arg}  seq={seq}"
    if msg_id == MSG_COMMAND_ACK and len(payload) == 5:
        d = decode_command_ack(payload)
        verdict = "accepted" if d["accepted"] else f"rejected (reason={d['reason']})"
        return f"COMMAND_ACK  cmd={d['cmd_id']}  seq={d['seq']}  {verdict}"
    if msg_id == MSG_HEARTBEAT and len(payload) == 11:
        d = decode_heartbeat(payload)
        return f"HEARTBEAT    uptime={d['uptime_ms']} ms  mode={d['mode']}  faults=0x{d['faults']:08X}  seq={d['seq']}"
    if msg_id == MSG_UART_STATUS and len(payload) == 16:
        overrun, framing, noise, dropped = struct.unpack("<IIII", payload)
        return (
            f"UART_STATUS  overrun={overrun}  framing={framing}  noise={noise}  dropped={dropped}"
        )
    if msg_id == MSG_IMU_DATA and len(payload) == 23:
        d = decode_imu(payload)
        return (
            f"IMU_DATA     t={d['t_ms']} ms  accel={d['accel']}  "
            f"gyro={d['gyro']}  mag={d['mag']}  flags=0x{d['flags']:02X}"
        )
    return f"msg 0x{msg_id:02X}  len={len(payload)}  payload={payload.hex()}"
