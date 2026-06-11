"""HIL runner - run a HIL scenario against the live board and grade what the link shows.

sil_runner's counterpart one level up the evidence ladder: the "shim" is the real
STM32, stimuli are bench actions declared as operator instructions (hold reset,
pull a jumper), and the only observable is the telemetry stream itself - at HIL
the ground sees exactly what a ground station sees. The LinkMonitor below is the
decision core: it owns no clock and no serial port, so all of its behavior is
unit-tested with injected time; the serial pump around it is deliberately dumb
plumbing whose proof is the bench.

Run: python tools/hil_runner.py COM5       (the whole campaign, prompts between scenarios)
     python tools/hil_runner.py COM5 2     (one scenario - number, name, or path)
"""

import argparse
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import yaml  # pip install pyyaml

from ground.frames import MSG_HEARTBEAT, FrameDecoder, decode_heartbeat
from ground.runner import (
    EXIT_PASS,
    REPO_ROOT,
    Check,
    die,
    print_verdict,
    repo_relative,
    resolve_scenarios,
    write_report,
)

EXPECT_KEYS = {"link", "seq_gaps", "crc_rejects", "period_s", "outage_s"}


class LinkMonitor:
    """Link-health state machine: no_link -> alive <-> lost; events fire on transitions."""

    def __init__(self, timeout_s=5.0):
        self.timeout_s = timeout_s
        self.state = "no_link"  # no_link / alive / lost
        self.last_hb_t = None  # when the last heartbeat arrived (None = never)
        self.last_seq = None
        self.crc_rejects = 0  # bumped by the serial pump, reported in stats
        # inter-arrival stats, incremental - never a list of every arrival
        self.period_count = 0
        self.period_sum = 0.0
        self.period_min = None
        self.period_max = None

    def on_frame(self, t, msg_id, payload) -> list:
        """A decoded frame arrived at time t; returns the events it caused."""
        if msg_id != MSG_HEARTBEAT:
            return []  # other messages don't feed the link timer
        seq = decode_heartbeat(payload)["seq"]

        events = []
        # react to the state as it is - the timeout decision lives in on_tick only
        if self.state == "no_link":
            events.append({"event": "link_up", "t": t})
        elif self.state == "lost":
            events.append({"event": "recovery", "t": t, "outage_s": t - self.last_hb_t})
        else:
            # nominal beat - the only case that feeds the stats (an outage gap would
            # poison min/mean/max)
            period = t - self.last_hb_t
            self.period_count += 1
            self.period_sum += period
            self.period_min = period if self.period_min is None else min(self.period_min, period)
            self.period_max = period if self.period_max is None else max(self.period_max, period)

        # seq gap check, independent of state (uint16 wrap ignored - 18 h at 1 Hz)
        if self.last_seq is not None and seq != self.last_seq + 1:
            events.append({"event": "seq_gap", "t": t, "missed": seq - self.last_seq - 1})

        # bookkeeping every heartbeat, gap or not
        self.state = "alive"
        self.last_hb_t = t
        self.last_seq = seq
        return events

    def on_tick(self, t) -> list:
        """Time passed with no frame; returns the link_loss event when one just happened.

        Loss can never be triggered by a frame - it is the absence of frames, and
        absent frames don't call functions. The caller asks this every loop pass.
        """
        # strict >, mirroring the satellite-side dead man; no_link can't go lost (a
        # link that never came up is the runner's finding, not the monitor's)
        if self.state == "alive" and (t - self.last_hb_t) > self.timeout_s:
            self.state = "lost"  # report once - the edge, not the level
            return [{"event": "link_loss", "t": t, "last_seen": self.last_hb_t}]
        return []

    def stats(self) -> dict:
        """Inter-arrival numbers for the report: count, min, mean, max, crc_rejects."""
        return {
            "count": self.period_count,
            "min": self.period_min,
            "mean": self.period_sum / self.period_count if self.period_count else None,
            "max": self.period_max,
            "crc_rejects": self.crc_rejects,
        }


# --- the conductor: scenario load, serial pump, grading, report (SSA-022) ---


@dataclass
class HilScenario:
    """One loaded HIL scenario file."""

    id: str
    title: str
    verifies: list
    duration_s: float
    operator: str
    timeout_s: float
    expect: dict
    path: Path


def load_scenario(path: Path) -> HilScenario:
    """Read and validate a HIL scenario YAML; missing or unknown keys die loudly."""
    try:
        with open(path) as f:
            data = yaml.safe_load(f)
    except OSError as e:
        die(f"cannot read scenario: {e}")
    except yaml.YAMLError as e:
        die(f"bad yaml in {path}: {e}")
    for key in ("id", "title", "duration_s", "expect"):
        if key not in data:
            die(f"scenario {path} is missing required key '{key}'")
    for key in data["expect"]:
        if key not in EXPECT_KEYS:
            die(f"unknown expect key '{key}' (allowed: {sorted(EXPECT_KEYS)})")
    return HilScenario(
        id=data["id"],
        title=data["title"],
        verifies=data.get("verifies", []),
        duration_s=float(data["duration_s"]),
        operator=data.get("operator", "none"),
        timeout_s=float(data.get("timeout_s", 5.0)),
        expect=data["expect"],
        path=path,
    )


def _fmt_event(e: dict) -> str:
    """One log line per event: time, name, then any extras."""
    extras = "  ".join(
        f"{k}={v:.2f}" if isinstance(v, float) else f"{k}={v}"
        for k, v in e.items()
        if k not in ("event", "t")
    )
    return f"t={e['t']:7.2f}s  {e['event']}" + (f"  {extras}" if extras else "")


def _fmt_stats(s: dict) -> str:
    """Stats block for the report evidence."""
    if not s["count"]:
        return f"no heartbeat periods measured\ncrc rejects: {s['crc_rejects']}"
    return (
        f"heartbeat periods: {s['count']}  "
        f"min {s['min']:.3f}s  mean {s['mean']:.3f}s  max {s['max']:.3f}s\n"
        f"crc rejects: {s['crc_rejects']}"
    )


def pump(port: str, baud: int, duration_s: float, mon: LinkMonitor) -> list:
    """Drive the monitor from a live serial port for the window; returns the event log.

    Deliberately decision-free: read bytes, decode, feed the monitor, narrate its
    events. on_tick runs every pass even when zero bytes arrive - a silent link
    delivers no bytes, and the timeout question must still be asked.
    """
    import serial  # lazy: hardware-only dep, keeps this module importable in CI

    decoder = FrameDecoder()
    log = []
    try:
        with serial.Serial(port, baud, timeout=0.1) as ser:
            start = time.monotonic()
            while (now := time.monotonic() - start) < duration_s:
                events = []
                for byte in ser.read(64):
                    frame = decoder.push(byte)
                    if frame is not None:
                        events += mon.on_frame(time.monotonic() - start, *frame)
                mon.crc_rejects = decoder.crc_errors
                events += mon.on_tick(now)
                for e in events:
                    log.append(e)
                    print(f"  {_fmt_event(e)}")
    except serial.SerialException as e:
        die(f"serial: {e}")
    return log


def grade(expect: dict, events: list, stats: dict) -> list:
    """Compare what the link showed against the scenario's expectations; pure."""
    checks = []
    names = [e["event"] for e in events]

    if "link" in expect:
        want = expect["link"]
        counts = (
            f"link_up x{names.count('link_up')}, link_loss x{names.count('link_loss')}, "
            f"recovery x{names.count('recovery')}"
        )
        if want == "up":
            ok = names.count("link_up") == 1 and names.count("link_loss") == 0
        elif want == "recovered":
            ok = (
                names.count("link_up") == 1
                and names.count("link_loss") == 1
                and names.count("recovery") == 1
            )
        else:
            die(f"unknown expect.link '{want}' (allowed: up, recovered)")
        checks.append(Check("link", want, counts, ok))

    if "seq_gaps" in expect:
        missed = sum(e["missed"] for e in events if e["event"] == "seq_gap")
        checks.append(
            Check(
                "seq_gaps",
                str(expect["seq_gaps"]),
                f"{missed} missed",
                missed == expect["seq_gaps"],
            )
        )

    if "crc_rejects" in expect:
        checks.append(
            Check(
                "crc_rejects",
                str(expect["crc_rejects"]),
                str(stats["crc_rejects"]),
                stats["crc_rejects"] == expect["crc_rejects"],
            )
        )

    if "period_s" in expect:
        want = expect["period_s"]
        ok = stats["count"] > 0 and want["min"] <= stats["min"] and stats["max"] <= want["max"]
        observed = (
            f"min {stats['min']:.3f}s max {stats['max']:.3f}s over {stats['count']} periods"
            if stats["count"]
            else "no periods measured"
        )
        checks.append(Check("period_s", f"{want['min']}..{want['max']} s", observed, ok))

    if "outage_s" in expect:
        recs = [e for e in events if e["event"] == "recovery"]
        ok = len(recs) == 1 and recs[0]["outage_s"] >= expect["outage_s"]["min"]
        observed = f"outage {recs[0]['outage_s']:.2f}s" if recs else "no recovery observed"
        checks.append(Check("outage_s", f">= {expect['outage_s']['min']} s", observed, ok))

    return checks


def run_scenario(sc: HilScenario, port: str, baud: int, verbose: bool) -> int:
    """One scenario end to end on the live link; returns its exit code."""
    print(f"{sc.id}  {sc.title}")
    print(f"  window: {sc.duration_s:.0f}s on {port} @ {baud}")
    print(f"  operator: {sc.operator}")

    mon = LinkMonitor(timeout_s=sc.timeout_s)
    events = pump(port, baud, sc.duration_s, mon)

    checks = grade(sc.expect, events, mon.stats())
    meta = [
        f"- Scenario: `{repo_relative(sc.path).as_posix()}`",
        f"- Verifies: {', '.join(sc.verifies) if sc.verifies else '(none listed)'}",
        f"- Window: {sc.duration_s:.0f} s, heartbeat timeout {sc.timeout_s:.0f} s",
    ]
    evidence = [
        ("Event log", "\n".join(_fmt_event(e) for e in events) or "(no events)"),
        ("Timing stats", _fmt_stats(mon.stats())),
    ]
    report = write_report(sc.id, sc.title, meta, checks, evidence, "hil")
    return print_verdict(sc.id, sc.title, checks, report, verbose)


def main() -> int:
    ap = argparse.ArgumentParser(description="run HIL scenarios against the live board")
    ap.add_argument("port", help="serial port, e.g. COM5 or /dev/ttyACM0")
    ap.add_argument(
        "scenarios",
        nargs="*",
        help="scenario refs: 'all' (default), a number (2), a name (link_loss), or a path",
    )
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument(
        "-v", "--verbose", action="store_true", help="print every check, not just failures"
    )
    args = ap.parse_args()

    files = resolve_scenarios(
        args.scenarios or ["all"], REPO_ROOT / "fsw" / "hil" / "scenarios", "hil"
    )

    results = []
    try:
        for path in files:
            sc = load_scenario(path)
            if len(files) > 1:
                # a human performs the stimuli, so a campaign pauses between scenarios
                input(f"\nnext: {sc.id} - press enter when the bench is ready... ")
            results.append(run_scenario(sc, args.port, args.baud, args.verbose))
    except KeyboardInterrupt:
        die("interrupted before the observation window ended")
    except EOFError:
        die("no stdin for the ready prompt - run campaigns from an interactive shell")

    if len(files) > 1:
        passed = sum(1 for r in results if r == EXIT_PASS)
        print(f"campaign: {passed}/{len(files)} scenarios passed (reports in docs/reports/hil/)")
    return max(results)


if __name__ == "__main__":
    sys.exit(main())
