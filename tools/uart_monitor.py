#!/usr/bin/env python3
"""Decode and print telemetry frames from the avionics node over serial.

Mirrors the firmware frame format (frame.c): [AA 55][id][len][payload][crc16].
Run: python tools/uart_monitor.py COMx (find the port x with: python -m serial.tools.list_ports)
"""

import argparse
import struct
import sys

import serial  # pyserial: pip install pyserial

SYNC0 = 0xAA
SYNC1 = 0x55

MSG_HEARTBEAT = 0x01
MSG_LINK_STATUS = 0x02


def crc16(data: bytes) -> int:
    """CRC-16/CCITT (poly 0x1021, init 0xFFFF) - matches frame.c."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


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
        return None


def format_frame(msg_id: int, payload: bytes) -> str:
    if msg_id == MSG_HEARTBEAT and len(payload) == 8:
        uptime, mode, faults, seq = struct.unpack("<IBBH", payload)
        return f"HEARTBEAT    uptime={uptime} ms  mode={mode}  faults=0x{faults:02X}  seq={seq}"
    if msg_id == MSG_LINK_STATUS and len(payload) == 16:
        overrun, framing, noise, dropped = struct.unpack("<IIII", payload)
        return (
            f"LINK_STATUS  overrun={overrun}  framing={framing}  noise={noise}  dropped={dropped}"
        )
    return f"msg 0x{msg_id:02X}  len={len(payload)}  payload={payload.hex()}"


def main() -> int:
    ap = argparse.ArgumentParser(description="monitor avionics node telemetry over serial")
    ap.add_argument("port", help="serial port, e.g. COM5 or /dev/ttyACM0")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--raw", action="store_true", help="also echo every raw byte as hex")
    args = ap.parse_args()

    decoder = FrameDecoder()
    with serial.Serial(args.port, args.baud, timeout=1) as ser:
        print(f"listening on {args.port} at {args.baud} 8N1 (ctrl-c to quit)")
        while True:
            for byte in ser.read(64):
                if args.raw:
                    sys.stdout.write(f"{byte:02X} ")
                    sys.stdout.flush()
                frame = decoder.push(byte)
                if frame is not None:
                    print(format_frame(*frame))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        pass
