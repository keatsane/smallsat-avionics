"""Bench driver for the reaction-wheel node.

Sends framed wheel commands over the ESC's serial link and prints the status frames it
answers with - the same wire format the OBC uses, so the bench and the flight path
exercise one protocol rather than two.

    python tools/wheel.py COM5 2.0      # 2.0 V on the q axis
    python tools/wheel.py COM5 0        # stop
    python tools/wheel.py COM5 --watch  # listen only, send nothing

The node coasts if it hears nothing for 500 ms, so a held target needs --hold.
"""

import argparse
import sys
import time

from ground.frames import FrameDecoder, encode_wheel_command, format_frame
from ground.link import find_port, open_port

KEEPALIVE_S = 0.2  # under the node's 500 ms dead-man


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("volts", nargs="?", type=float, help="q-axis target in volts")
    # a flag, not a positional ahead of volts - see the same fix in hil_runner.py. an optional
    # positional in front of a real argument silently eats it, so `wheel.py 3.5` set no target
    # and tried to open a serial port called "3.5"
    ap.add_argument("--port", help="serial port; omit to find it by ST-Link serial")
    ap.add_argument("--stlink", help="pin the port by ST-Link serial number")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--hold", action="store_true", help="keep resending so the target persists")
    ap.add_argument("--watch", action="store_true", help="listen only, send nothing")
    ap.add_argument("--raw", action="store_true", help="dump bytes as text, no frame decoding")
    args = ap.parse_args()

    if args.raw:
        args.watch = True
    if args.volts is None and not args.watch:
        ap.error("give a target in volts, or --watch to listen only")

    dec = FrameDecoder()
    seq = 0
    next_send = 0.0

    with open_port(args.port or find_port(args.stlink), args.baud) as ser:
        while True:
            now = time.monotonic()
            if not args.watch and now >= next_send:
                seq = (seq + 1) & 0xFFFF
                ser.write(encode_wheel_command(int(args.volts * 1000), seq))
                if not args.hold:
                    args.watch = True  # one shot, then just report
                next_send = now + KEEPALIVE_S

            chunk = ser.read(256)
            if args.raw:
                if chunk:
                    print(chunk.decode("latin-1"), end="", flush=True)
                continue

            for byte in chunk:
                frame = dec.push(byte)
                if frame:
                    print(format_frame(*frame), flush=True)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(0)  # ctrl-c is how you stop it, not a failure
