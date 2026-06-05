#!/usr/bin/env python3
"""Decode and print telemetry frames from the avionics node over serial.

Run: python tools/uart_monitor.py COMx (find the port x with: python -m serial.tools.list_ports)
"""

import argparse
import sys

import serial  # pyserial: pip install pyserial
from frames import FrameDecoder, format_frame


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
