#!/usr/bin/env python3
"""Decode and print telemetry frames from the avionics node over serial.

Run: python tools/uart_monitor.py COMx (find the port x with: python -m serial.tools.list_ports)
"""

import argparse
import sys

import serial  # pyserial: pip install pyserial
from ground.frames import FrameDecoder, format_frame


def main() -> int:
    ap = argparse.ArgumentParser(description="monitor avionics node telemetry over serial")
    ap.add_argument("port", help="serial port, e.g. COM5 or /dev/ttyACM0")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--raw", action="store_true", help="also echo every raw byte as hex")
    # kinds: HEARTBEAT, POWER_DATA, IMU_DATA, COMMAND, COMMAND_ACK, UART_STATUS, TEXT
    ap.add_argument(
        "--only", help="show only these kinds, comma-separated (e.g. HEARTBEAT,POWER_DATA)"
    )
    ap.add_argument("--hide", help="hide these kinds, comma-separated (e.g. IMU_DATA,TEXT)")
    args = ap.parse_args()

    only = {s.strip().upper() for s in args.only.split(",")} if args.only else set()
    hide = {s.strip().upper() for s in args.hide.split(",")} if args.hide else set()

    def show(line: str) -> None:
        # a line's kind is its leading token (HEARTBEAT, POWER_DATA, TEXT, ...)
        kind = line.split(maxsplit=1)[0] if line.strip() else ""
        if only and kind not in only:
            return
        if kind in hide:
            return
        print(line)

    decoder = FrameDecoder()
    text = bytearray()  # printable bytes between frames - firmware debug prints

    def flush_text() -> None:
        if text:
            show(f"{'TEXT':<12} {text.decode('ascii')}")
            text.clear()

    with serial.Serial(args.port, args.baud, timeout=1) as ser:
        print(f"listening on {args.port} at {args.baud} 8N1 (ctrl-c to quit)")
        while True:
            data = ser.read(64)
            if not data:
                flush_text()  # idle - show any half line rather than sitting on it
                continue
            for byte in data:
                if args.raw:
                    sys.stdout.write(f"{byte:02X} ")
                    sys.stdout.flush()
                hunting = decoder.state == "sync0"
                frame = decoder.push(byte)
                if frame is not None:
                    show(format_frame(*frame))
                elif hunting and decoder.state == "sync0":
                    # stray byte outside any frame: debug text from the firmware
                    if byte == 0x0A:
                        flush_text()
                    elif 0x20 <= byte < 0x7F:
                        text.append(byte)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        pass
