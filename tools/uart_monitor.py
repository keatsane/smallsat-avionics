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

from ground.colors import LINK_COLORS, colorize_heartbeat, paint
from ground.commands import CommandError, parse, usage
from ground.console import make_session
from ground.filters import KINDS, QUIET_KINDS, Filters
from ground.frames import (
    MSG_COMMAND_ACK,
    MSG_DOWNLINK_STATUS,
    MSG_GROUND_STATUS,
    MSG_PAYLOAD_DATA,
    FrameDecoder,
    decode_command_ack,
    decode_downlink_status,
    decode_ground_status,
    decode_payload_data,
    encode_command,
    format_frame,
)
from ground.link import find_port, open_port
from ground.payload import Assembler, looks_like_jpeg

KEEPALIVE_S = 2.0  # under the flight software's 5 s command-loss timeout

# a command is retransmitted until the vehicle acknowledges it. the lora link is half duplex and
# the vehicle is deaf for the ~57 ms its beacon is on the air, so a command sent at the wrong
# moment is simply not heard - on the bench that lost about one in six. the flight software drops
# a repeated sequence number rather than acting twice, so a resend costs nothing when the command
# did arrive and only the ack was lost.
#
# the interval is deliberately not a multiple of the 1 s beacon: a retry that keeps landing in the
# same relative slot would keep hitting the same deaf window forever
RETRY_AFTER_S = 0.7
RETRY_LIMIT = 4  # gives up after ~2.8 s, well inside the 5 s command-loss timeout

# how long a scripted run keeps listening after its piped commands are sent. long enough for an
# ack and the next heartbeat, short enough that it never becomes the process nobody knows is
# holding the port
SCRIPT_DRAIN_S = 2.5
NOOP_ID = 0  # first entry of FSW_COMMAND_LIST


# a downlinking image is ~130 frames; one line each would bury everything else, so progress is
# reported on completion and at intervals rather than per chunk
PAYLOAD_PROGRESS_EVERY = 25


def directives() -> str:
    """The console's own commands - local, never transmitted."""
    return (
        "\n  console directives (local, never sent to the spacecraft):\n"
        "    /all                    show every kind\n"
        "    /quiet                  hide the per-cycle stream (the startup default)\n"
        "    /only HEARTBEAT,POWER   show nothing else\n"
        "    /show IMU,TEMP          stop hiding these\n"
        "    /hide IMU               hide these\n"
        "    /filters                what is showing right now\n"
        f"    kinds: {', '.join(KINDS)}"
    )


def main() -> int:
    ap = argparse.ArgumentParser(description="ground console for the avionics node")
    ap.add_argument("port", nargs="?", help="serial port; omit to find it automatically")
    ap.add_argument("--stlink", help="pin the port by ST-Link serial number")
    ap.add_argument("--vid", help="pin the port by USB vendor id in hex, e.g. 239A for adafruit")
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
        "--no-retry", action="store_true", help="send each command once, never retransmit"
    )
    ap.add_argument(
        "--images", default="captures", help="directory for downlinked images (default: captures)"
    )
    ap.add_argument(
        "--all",
        action="store_true",
        help=f"also show the quiet kinds ({', '.join(QUIET_KINDS)})",
    )
    args = ap.parse_args()

    only = {s.strip().upper() for s in args.only.split(",")} if args.only else set()
    hide = {s.strip().upper() for s in args.hide.split(",")} if args.hide else set()

    # quiet by default so the readable traffic - heartbeats, acks, firmware text - stays readable.
    # naming a filter explicitly means you know what you want, so the default gets out of the way.
    # changeable at the prompt afterwards with /show, /hide, /only, /all, /quiet
    default_quiet = not (args.all or only or hide)
    filters = Filters.quiet() if default_quiet else Filters(only=only, hide=hide)

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
        "DOWNLINK": "\x1b[1;35m",  # bold magenta - the progress of that bulk data
        "TASKS": "\x1b[36m",  # cyan, plainer than the heartbeat's - same cadence, less to read
        "LINKERR": "\x1b[1;31m",  # bold red - a frame arrived corrupt, which is never routine
        "FILTER": "\x1b[1;34m",  # bold blue - console state, not anything the spacecraft said
        "GROUND": "\x1b[36m",  # cyan - the ground station talking about itself, not the vehicle
    }

    # rebound to prompt_toolkit's printer once the prompt owns the terminal (see console.py)
    def emit(line: str) -> None:
        print(line, flush=True)

    def show(line: str) -> None:
        # a line's kind is its leading token (HEARTBEAT, POWER, TEXT, ...)
        kind = line.split(maxsplit=1)[0] if line.strip() else ""
        if not filters.visible(kind):
            return

        # the heartbeat is painted field by field rather than as one color, because the fields are
        # what the satellite's own status beads show: mode carries bead 0's color, and each fault
        # carries bead 1's - blue when latched but inhibited, red when something is acting on it.
        # someone who has learned the rig's colors has learned the console's
        if kind == "HEARTBEAT":
            emit(colorize_heartbeat(line, use_color))
            return
        if kind == "GROUND" and use_color:
            for word, key in (("up", "up"), ("DOWN", "lost")):
                line = line.replace(f" {word} ", f" {paint(word, LINK_COLORS[key])} ")
            emit(line)
            return

        color = kind_color.get(kind)
        emit(f"{color}{line}\x1b[0m" if use_color and color else line)

    port = args.port or find_port(args.stlink, args.vid)
    ser = open_port(port, args.baud, timeout=0.1)

    tx = (
        threading.Lock()
    )  # the reader thread's keep-alive, its retries, and the prompt all transmit
    seq = 0
    outstanding: dict[int, dict] = {}  # seq -> what was sent and when to send it again

    def send(cmd_id: int, arg: int, label: str, retry: bool = True) -> None:
        nonlocal seq
        with tx:
            seq = (seq + 1) & 0xFFFF
            frame = encode_command(cmd_id, arg, seq)
            ser.write(frame)
            show(f"{'SENT':<12} {label}  seq={seq}")
            if retry and not args.no_retry:
                outstanding[seq] = {
                    "frame": frame,
                    "label": label,
                    "tries": 1,
                    "due": time.monotonic() + RETRY_AFTER_S,
                }

    def service_retries() -> None:
        """Resend anything still unacknowledged, and say so when one is given up on."""
        now = time.monotonic()
        with tx:
            for s_no, p in list(outstanding.items()):
                if now < p["due"]:
                    continue
                if p["tries"] >= RETRY_LIMIT:
                    del outstanding[s_no]
                    show(
                        f"{'LINKERR':<12} no ack for {p['label']}  seq={s_no} "
                        f"after {RETRY_LIMIT} tries"
                    )
                    continue
                p["tries"] += 1
                p["due"] = now + RETRY_AFTER_S
                ser.write(p["frame"])
                show(f"{'SENT':<12} {p['label']}  seq={s_no}  (retry {p['tries']})")

    def local_directive(line: str) -> bool:
        """Handle a console directive. Returns True if the line was one.

        Directives start with '/' so they can never be confused with a spacecraft command, now or
        when the command catalog grows. Filtering has to be changeable while running: which lines
        are worth seeing changes minute to minute, and quitting to pass a different flag loses the
        session - the link, any reassembly in progress, and the scrollback.
        """
        if line in ("?", "help"):
            print(usage())
            print(directives())
            return True
        if not line.startswith("/"):
            return False

        parts = line[1:].split(maxsplit=1)
        verb = parts[0].lower() if parts else ""
        kinds = {k.strip().upper() for k in parts[1].split(",")} if len(parts) > 1 else set()

        if verb in ("?", "help"):
            print(directives())
        elif not filters.apply(verb, kinds):
            show(f"{'REJECT':<12} unknown directive {line!r} - /? lists them")
            return True

        show(f"{'FILTER':<12} {filters.describe()}")
        return True

    def submit(line: str) -> None:
        """One typed or piped line -> a console directive, a command on the wire, or a rejection."""
        line = line.strip()
        if not line or local_directive(line):
            return
        try:
            cmd_id, arg = parse(line)
        except CommandError as e:
            show(f"{'REJECT':<12} {e}")  # refused here, never put on the wire
            return
        send(cmd_id, arg, line.upper())

    # what each end counted at the start of the current downlink pass, so the end of it can be
    # reported as a delivery rate rather than as two numbers nobody wants to subtract by hand.
    # both are already on the wire - the satellite's packet count rides the progress frame and the
    # ground station's rides its own health frame - and the pass is the only place they mean
    # anything together
    pass_start: dict | None = None
    ground_now = {"packets": 0, "frames": 0}

    def note_pass(d: dict) -> None:
        """Watch a downlink begin and end, and say what fraction of it arrived."""
        nonlocal pass_start
        if d["chunks"] > 0:
            if pass_start is None:
                pass_start = {
                    "sent": d["radio_sent"],
                    "packets": ground_now["packets"],
                    "frames": ground_now["frames"],
                    "chunks": d["chunks"],
                }
            return
        if pass_start is None:
            return  # idle, and it was idle before - nothing to close out

        sent = d["radio_sent"] - pass_start["sent"]
        got = ground_now["packets"] - pass_start["packets"]
        frames = ground_now["frames"] - pass_start["frames"]
        pct = (got * 100 // sent) if sent else 0
        show(
            f"{'LINK':<12} pass done  {pass_start['chunks']} chunks  "
            f"packets {got}/{sent} ({pct}%)  frames {frames}"
        )
        pass_start = None

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
                send(NOOP_ID, 0, "NOOP (keepalive)", retry=False)
                next_keepalive = time.monotonic() + KEEPALIVE_S

            if outstanding:
                service_retries()

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
                        if frame[0] == MSG_COMMAND_ACK:
                            outstanding.pop(decode_command_ack(frame[1])["seq"], None)
                        elif frame[0] == MSG_GROUND_STATUS:
                            g = decode_ground_status(frame[1])
                            ground_now["packets"] = g["nrf24_packets"]
                            ground_now["frames"] = g["nrf24_frames"]
                        elif frame[0] == MSG_DOWNLINK_STATUS:
                            note_pass(decode_downlink_status(frame[1]))
                        show(format_frame(*frame))
                elif hunting and decoder.state == "sync0":
                    # stray byte outside any frame: debug text from the firmware
                    if byte == 0x0A:
                        flush_text()
                    elif 0x20 <= byte < 0x7F:
                        text.append(byte)

    print(f"listening on {port} at {args.baud} 8N1 (ctrl-c to quit)")
    if default_quiet:
        print(f"hidden by default ({', '.join(QUIET_KINDS)}) - --all or /show brings them back")
    print("TASKS reads <task>: <state> <stack words still free> <age of last check-in>")

    interactive = not args.read_only and sys.stdin.isatty()
    if interactive:
        # a pinned prompt needs a terminal - patch_stdout keeps the reader thread's output
        # scrolling above the input line instead of overwriting what is half typed
        # show() closes over emit, so this rebind takes
        session, emit, patch_stdout = make_session(KINDS)
        print("tab completes, up-arrow recalls, ? lists commands and /? the console directives")

    threading.Thread(target=reader, daemon=True).start()

    try:
        if args.read_only:
            # polled rather than a bare stop.wait(): an untimed Event.wait() blocks inside the C
            # layer on windows and never returns to python to run the signal handler, so ctrl-c
            # is swallowed and the only way out is closing the terminal
            while not stop.wait(0.2):
                pass
        elif interactive:
            with patch_stdout():
                while True:
                    submit(session.prompt())
        else:
            for line in sys.stdin:  # piped in - same syntax, no prompt
                submit(line)
            # drain briefly so the acks for what was just sent arrive, then exit. waiting forever
            # here would leave a scripted run holding the port with nothing left to do, and the
            # next session finds the link busy with no terminal to blame
            stop.wait(SCRIPT_DRAIN_S)
    except (EOFError, KeyboardInterrupt):
        pass
    finally:
        stop.set()
        ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
