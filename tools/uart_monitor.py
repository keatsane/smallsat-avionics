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

This file is deliberately just the terminal: argument parsing, the prompt, colors, and a serial
port. Everything with protocol state - retries, pass accounting, selective repeat - lives in
ground/session.py, where the tests can reach it.
"""

import argparse
import os
import sys
import threading
import time

from pathlib import Path

from ground.colors import LINK_COLORS, colorize_heartbeat, paint
from ground.commands import ARG_CATALOG, CommandError, parse, usage
from ground.console import make_session
from ground.filters import KINDS, QUIET_KINDS, Filters
from ground.frames import COMMANDS, arg_label
from ground.link import find_port, open_port
from ground.payload import Assembler
from ground.session import GroundSession

KEEPALIVE_S = 2.0  # under the flight software's 5 s command-loss timeout

# how long a scripted run keeps listening after its piped commands are sent. long enough for an
# ack and the next heartbeat, short enough that it never becomes the process nobody knows is
# holding the port
SCRIPT_DRAIN_S = 2.5
NOOP_ID = 0  # first entry of FSW_COMMAND_LIST


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
        "    shoot [size] [bearing]  the whole imaging sequence as one command\n"
        "    poll IMU|POWER|TEMP|... one frame of that kind over the radio, on request\n"
        f"    kinds: {', '.join(KINDS)}"
    )


def main() -> int:
    ap = argparse.ArgumentParser(description="ground console for the avionics node")
    ap.add_argument("port", nargs="?", help="serial port; omit to find it automatically")
    ap.add_argument("--stlink", help="pin the port by ST-Link serial number")
    ap.add_argument("--vid", help="pin the port by USB vendor id in hex, e.g. 239A for adafruit")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--raw", action="store_true", help="also echo every raw byte as hex")
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
        "REQUEST": "\x1b[1;32m",  # ditto - the ground naming the chunks it wants again
        "MISSION": "\x1b[1;34m",  # bold blue - the shoot macro narrating its steps
        "REJECT": "\x1b[1;31m",  # bold red - refused here, never reached the wire
        "PAYLOAD": "\x1b[35m",  # magenta - bulk data, distinct from the health stream
        "DOWNLINK": "\x1b[1;35m",  # bold magenta - the progress of that bulk data
        "LINK": "\x1b[1;35m",  # the same, since it is that pass's closing line
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

    session = GroundSession(
        Assembler(Path(args.images)), retry=not args.no_retry, transmit=not args.read_only
    )
    port_lock = threading.Lock()  # the reader thread and the prompt both write the port

    def deliver(lines: list, tx: list) -> None:
        """One session result -> the port and the screen."""
        with port_lock:
            for frame in tx:
                ser.write(frame)
        for line in lines:
            show(line)

    def shoot(argv: list) -> None:
        """The whole imaging sequence as one word: point, aim, capture, downlink, park."""
        res, bearing = None, None
        for a in argv:
            if "x" in a.lower():
                res = a.lower()
            else:
                try:
                    bearing = float(a)
                except ValueError:
                    show(f"{'REJECT':<12} shoot takes a size and/or a bearing, not {a!r}")
                    return
        deliver(*session.mission_start(res, bearing, time.monotonic()))

    def submit(line: str) -> None:
        """One typed or piped line -> a console directive, a command on the wire, or a rejection."""
        line = line.strip()
        if not line or local_directive(line):
            return
        parts_ = line.split()
        if parts_[0].lower() == "shoot":
            shoot(parts_[1:])
            return
        try:
            cmd_id, arg = parse(line)
        except CommandError as e:
            show(f"{'REJECT':<12} {e}")  # refused here, never put on the wire
            return
        # echo the canonical form rather than the typed line - upper-casing what was typed turned
        # 800x600 into 800X600, a name that exists nowhere else in the system. catalog arguments
        # come back as their catalog names; SET_HEADING echoes the degrees as typed, since the
        # wire value is a binary angle nobody thinks in
        parts = line.split()
        name = COMMANDS[cmd_id]
        if name in ARG_CATALOG:
            label = f"{name} {arg_label(cmd_id, arg)}"
        elif len(parts) > 1:
            label = f"{name} {parts[1]}"
        else:
            label = name
        deliver(*session.command(cmd_id, arg, label, time.monotonic()))

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

    stop = threading.Event()

    def reader() -> None:
        next_keepalive = 0.0

        while not stop.is_set():
            now = time.monotonic()
            if args.keepalive and not args.read_only and now >= next_keepalive:
                deliver(*session.command(NOOP_ID, 0, "NOOP (keepalive)", now, retry=False))
                next_keepalive = now + KEEPALIVE_S

            deliver(*session.tick(now))

            data = ser.read(64)
            if not data:
                for line in session.idle():
                    show(line)
                continue
            if args.raw:
                sys.stdout.write("".join(f"{b:02X} " for b in data))
                sys.stdout.flush()
            deliver(*session.feed(data, time.monotonic()))

    print(f"listening on {port} at {args.baud} 8N1 (ctrl-c to quit)")
    if default_quiet:
        print(f"hidden by default ({', '.join(QUIET_KINDS)}) - --all or /show brings them back")
    print("TASKS reads <task>: <state> <stack words still free> <age of last check-in>")

    interactive = not args.read_only and sys.stdin.isatty()
    if interactive:
        # a pinned prompt needs a terminal - patch_stdout keeps the reader thread's output
        # scrolling above the input line instead of overwriting what is half typed
        # show() closes over emit, so this rebind takes
        prompt_session, emit, patch_stdout = make_session(KINDS)
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
                    submit(prompt_session.prompt())
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
