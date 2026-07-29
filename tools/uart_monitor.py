#!/usr/bin/env python3
"""Ground console for the avionics node - decoded telemetry out, typed commands in.

A serial port takes one program at a time, so watching and commanding cannot be two tools on one
link. Telemetry scrolls above a prompt pinned to the bottom of the terminal; tab completes command
and argument names, and up-arrow recalls what you sent.

    just obc-monitor                 # port found by the obc's st-link serial
    just obc-monitor -- --keepalive  # plus a NOOP every 2 s

    cmd> SET_MODE DETUMBLE
    cmd> ?                           list what can be typed

Commands go out on the link the telemetry comes back on, so a command, its ack, and the telemetry
showing its effect land in one stream in order. Piping commands in instead of typing them scripts
the same syntax: `echo SET_MODE STANDBY | python tools/uart_monitor.py`.
"""

import argparse
import os
import sys
import threading
import time

from pathlib import Path

from ground.commands import CommandError, parse, usage
from ground.console import make_session
from ground.frames import (
    MSG_PAYLOAD_DATA,
    FrameDecoder,
    decode_payload_data,
    encode_command,
    format_frame,
)
from ground.link import find_port, open_port
from ground.payload import Assembler, looks_like_jpeg

KEEPALIVE_S = 2.0  # under the flight software's 5 s command-loss timeout
NOOP_ID = 0  # first entry of FSW_COMMAND_LIST

# the obc emits these every control cycle - four kinds at 10 Hz is forty lines a second, which
# scrolls anything you actually wanted to read off the screen before you can read it. hidden by
# default and announced at startup, because silently dropping telemetry would be worse
PER_CYCLE_KINDS = ("IMU", "POWER", "TEMP", "CAMERA")

# a downlinking image is ~130 frames; one line each would bury everything else, so progress is
# reported on completion and at intervals rather than per chunk
PAYLOAD_PROGRESS_EVERY = 25


def main() -> int:
    ap = argparse.ArgumentParser(description="ground console for the avionics node")
    ap.add_argument("port", nargs="?", help="serial port; omit to find it automatically")
    ap.add_argument("--stlink", help="pin the port by ST-Link serial number")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--raw", action="store_true", help="also echo every raw byte as hex")
    # kinds: HEARTBEAT, IMU, POWER, TEMP, CAMERA, UART, COMMAND, COMMAND_ACK,
    # WHEEL_CMD, WHEEL_STATUS, TEXT, SENT, REJECT
    ap.add_argument("--only", help="show only these kinds, comma-separated (e.g. HEARTBEAT,POWER)")
    ap.add_argument("--hide", help="hide these kinds, comma-separated (e.g. IMU,TEXT)")
    ap.add_argument("--no-color", action="store_true", help="plain output, no ANSI color")
    ap.add_argument(
        "--keepalive", action="store_true", help="send NOOP every 2 s to hold off COMMAND_LINK_LOSS"
    )
    ap.add_argument("--read-only", action="store_true", help="never transmit, ignore input")
    ap.add_argument(
        "--images", default="captures", help="directory for downlinked images (default: captures)"
    )
    ap.add_argument(
        "--all",
        action="store_true",
        help=f"also show the per-cycle stream ({', '.join(PER_CYCLE_KINDS)})",
    )
    args = ap.parse_args()

    only = {s.strip().upper() for s in args.only.split(",")} if args.only else set()
    hide = {s.strip().upper() for s in args.hide.split(",")} if args.hide else set()

    # quiet by default so the readable traffic - heartbeats, acks, firmware text - stays readable.
    # naming a filter explicitly means you know what you want, so the default gets out of the way
    default_quiet = not (args.all or only or hide)
    if default_quiet:
        hide = set(PER_CYCLE_KINDS)

    # color the kind when writing to a terminal (piped output stays plain): heartbeat stands out,
    # commands/status are flagged, firmware text is dimmed, the high-rate sensor lines stay default
    use_color = sys.stdout.isatty() and not args.no_color
    if use_color and sys.platform == "win32":
        os.system("")  # enable ANSI escape processing in the windows console
    kind_color = {
        "HEARTBEAT": "\x1b[1;36m",  # bold cyan
        "COMMAND": "\x1b[33m",
        "COMMAND_ACK": "\x1b[33m",
        "UART": "\x1b[33m",
        "TEXT": "\x1b[2m",  # dim
        "SENT": "\x1b[1;32m",  # bold green - the local side of the conversation
        "REJECT": "\x1b[1;31m",  # bold red - refused here, never reached the wire
        "PAYLOAD": "\x1b[35m",  # magenta - bulk data, distinct from the health stream
        "TASKS": "\x1b[36m",  # cyan, plainer than the heartbeat's - same cadence, less to read
        "LINKERR": "\x1b[1;31m",  # bold red - a frame arrived corrupt, which is never routine
    }

    # rebound to prompt_toolkit's printer once the prompt owns the terminal (see console.py)
    def emit(line: str) -> None:
        print(line, flush=True)

    def show(line: str) -> None:
        # a line's kind is its leading token (HEARTBEAT, POWER, TEXT, ...)
        kind = line.split(maxsplit=1)[0] if line.strip() else ""
        # the local side of the conversation is never filtered - hiding what you just typed
        # because of a --only flag is how you sit there wondering why nothing happened
        if kind not in ("SENT", "REJECT"):
            if only and kind not in only:
                return
            if kind in hide:
                return
        color = kind_color.get(kind)
        emit(f"{color}{line}\x1b[0m" if use_color and color else line)

    port = args.port or find_port(args.stlink)
    ser = open_port(port, args.baud, timeout=0.1)

    tx = threading.Lock()  # the reader thread's keep-alive and the prompt both transmit
    seq = 0

    def send(cmd_id: int, arg: int, label: str) -> None:
        nonlocal seq
        with tx:
            seq = (seq + 1) & 0xFFFF
            ser.write(encode_command(cmd_id, arg, seq))
            show(f"{'SENT':<12} {label}  seq={seq}")

    def submit(line: str) -> None:
        """One typed or piped line -> a command on the wire, or a local rejection."""
        line = line.strip()
        if not line:
            return
        if line in ("?", "help"):
            print(usage())
            return
        try:
            cmd_id, arg = parse(line)
        except CommandError as e:
            show(f"{'REJECT':<12} {e}")  # refused here, never put on the wire
            return
        send(cmd_id, arg, line.upper())

    decoder = FrameDecoder()
    text = bytearray()  # printable bytes between frames - firmware debug prints
    stop = threading.Event()
    assembler = Assembler(Path(args.images))

    def take_chunk(msg_id: int, payload: bytes) -> None:
        """One image chunk: reassemble, report sparingly, write the file when it is whole."""
        d = decode_payload_data(payload)
        img, note = assembler.push(d)
        if img is None:
            if d["chunk"] % PAYLOAD_PROGRESS_EVERY == 0:
                show(f"{'PAYLOAD':<12} {note}")
            return

        path = assembler.save(img)
        # the same SOI/EOI check the firmware makes, repeated at the other end of the link - it is
        # the one thing that proves the bytes survived framing, chunking, and reassembly intact
        verdict = "JPEG OK" if looks_like_jpeg(img.data()) else "BAD MARKERS"
        show(f"{'PAYLOAD':<12} {note} -> {path} ({verdict})")

    def reader() -> None:
        next_keepalive = 0.0

        def flush_text() -> None:
            if text:
                show(f"{'TEXT':<12} {text.decode('ascii')}")
                text.clear()

        while not stop.is_set():
            if args.keepalive and not args.read_only and time.monotonic() >= next_keepalive:
                send(NOOP_ID, 0, "NOOP (keepalive)")
                next_keepalive = time.monotonic() + KEEPALIVE_S

            data = ser.read(64)
            if not data:
                flush_text()  # idle - show any half line rather than sitting on it
                continue
            for byte in data:
                if args.raw:
                    sys.stdout.write(f"{byte:02X} ")
                    sys.stdout.flush()
                hunting = decoder.state == "sync0"
                crc_before = decoder.crc_errors
                frame = decoder.push(byte)
                # a corrupt frame was being counted and never reported, which made a link problem
                # look like stray debug text. say so the moment it happens
                if decoder.crc_errors != crc_before:
                    show(f"{'LINKERR':<12} bad crc - frames dropped={decoder.crc_errors}")
                if frame is not None:
                    if frame[0] == MSG_PAYLOAD_DATA:
                        take_chunk(*frame)
                    else:
                        show(format_frame(*frame))
                elif hunting and decoder.state == "sync0":
                    # stray byte outside any frame: debug text from the firmware
                    if byte == 0x0A:
                        flush_text()
                    elif 0x20 <= byte < 0x7F:
                        text.append(byte)

    print(f"listening on {port} at {args.baud} 8N1 (ctrl-c to quit)")
    if default_quiet:
        print(f"per-cycle telemetry hidden ({', '.join(PER_CYCLE_KINDS)}) - --all shows it")
    print("TASKS reads <task>: <state> <stack words still free> <age of last check-in>")

    interactive = not args.read_only and sys.stdin.isatty()
    if interactive:
        # a pinned prompt needs a terminal - patch_stdout keeps the reader thread's output
        # scrolling above the input line instead of overwriting what is half typed
        session, emit, patch_stdout = (
            make_session()
        )  # show() closes over emit, so this rebind takes
        print("tab completes, up-arrow recalls, ? lists commands")

    threading.Thread(target=reader, daemon=True).start()

    try:
        if args.read_only:
            stop.wait()
        elif interactive:
            with patch_stdout():
                while True:
                    submit(session.prompt())
        else:
            for line in sys.stdin:  # piped in - same syntax, no prompt
                submit(line)
            stop.wait()
    except (EOFError, KeyboardInterrupt):
        pass
    finally:
        stop.set()
        ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
