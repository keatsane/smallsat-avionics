#!/usr/bin/env python3
"""Run a SIL scenario: load the YAML, drive the shim, grade the output, write the report.

The runner is scenario-agnostic (REQ-VV-001): it knows the shim's I/O contract and the
expectation grammar, never a specific fault or scenario. New scenarios are new YAML files
under fsw/sil/scenarios/, not runner changes. Each run writes docs/reports/<id>.md with
observed-vs-expected per check (REQ-VV-002).

Run: python tools/sil_runner.py fsw/sil/scenarios/sil_001_undervoltage.yaml
"""

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import yaml  # pip install pyyaml

from ground.frames import MSG_COMMAND_ACK, FrameDecoder, decode_command_ack
from ground.runner import EXIT_PASS, REPO_ROOT, Check, die, print_verdict, write_report

SHIM_TIMEOUT_S = 10

EXPECT_KEYS = {"mode_log", "acks", "final"}
FINAL_KEYS = {"mode", "faults_set"}


@dataclass
class Scenario:
    """One loaded scenario file."""

    id: str
    title: str
    verifies: list
    timeline: list
    expect: dict
    path: Path


@dataclass
class Event:
    """One parsed line of shim output (CYCLE / EVENT / END)."""

    tag: str  # CYCLE, EVENT, END
    kind: str  # mode / fault / cmd for EVENT lines, "" otherwise
    fields: dict  # the line's key=value pairs, as strings


def load_scenario(path: Path) -> Scenario:
    """Read and validate a scenario YAML; missing required keys die loudly."""
    try:
        with open(path) as f:
            data = yaml.safe_load(f)
    except OSError as e:
        die(f"cannot read scenario: {e}")
    except yaml.YAMLError as e:
        die(f"bad yaml in {path}: {e}")
    for key in ("id", "title", "timeline", "expect"):
        if key not in data:
            die(f"scenario {path} is missing required key '{key}'")
    for key in data["expect"]:
        if key not in EXPECT_KEYS:
            die(f"unknown expect key '{key}' (allowed: {sorted(EXPECT_KEYS)})")
    for key in data["expect"].get("final", {}):
        if key not in FINAL_KEYS:
            die(f"unknown expect.final key '{key}' (allowed: {sorted(FINAL_KEYS)})")
    return Scenario(
        id=data["id"],
        title=data["title"],
        verifies=data.get("verifies", []),
        timeline=data["timeline"],
        expect=data["expect"],
        path=path,
    )


def compile_timeline(timeline: list) -> str:
    """YAML steps -> shim input lines: '<t> [fault NAME 0|1] [cmd NAME arg seq]'."""
    lines = []
    for step in timeline:
        if "t" not in step:
            die(f"timeline step missing 't': {step}")
        parts = [str(step["t"])]
        if "fault" in step:
            f = step["fault"]
            parts += ["fault", str(f["name"]), str(f["bad"])]
        if "cmd" in step:
            c = step["cmd"]
            parts += ["cmd", str(c["name"]), str(c["arg"]), str(c["seq"])]
        lines.append(" ".join(parts))
    return "\n".join(lines) + "\n"


def find_shim() -> Path:
    """The built shim executable (windows or posix name)."""
    for name in ("sil_shim.exe", "sil_shim"):
        path = REPO_ROOT / "fsw" / "build" / name
        if path.exists():
            return path
    die("shim not built - run `just build-fsw` first")


def run_shim(timeline_text: str) -> str:
    """Feed the timeline to the shim over stdin, return its stdout."""
    shim = find_shim()
    try:
        result = subprocess.run(
            [str(shim)],
            input=timeline_text,
            capture_output=True,
            text=True,
            timeout=SHIM_TIMEOUT_S,
        )
    except subprocess.TimeoutExpired:
        die(f"shim did not finish within {SHIM_TIMEOUT_S}s")
    if result.returncode != 0:
        # the shim rejected the timeline - the scenario file is broken, not the satellite
        die(f"shim exited {result.returncode}: {result.stderr.strip()}")
    return result.stdout


def _kv(parts: list) -> dict:
    """['t=100', 'mode=SAFE'] -> {'t': '100', 'mode': 'SAFE'}."""
    return dict(p.split("=", 1) for p in parts)


def parse_output(stdout: str) -> tuple:
    """Shim stdout -> (Event records, decoded ack frames); every FRAME line must CRC-check."""
    events = []
    acks = []
    decoder = FrameDecoder()
    for line in stdout.splitlines():
        parts = line.split()
        if not parts:
            continue
        tag = parts[0]
        if tag == "FRAME":
            try:
                raw = bytes.fromhex(parts[1])
            except (IndexError, ValueError):
                die(f"unparseable FRAME line: '{line}'")
            frame = None
            for byte in raw:
                frame = decoder.push(byte) or frame
            if frame is None:
                die(f"frame failed crc/framing: '{line}'")
            msg_id, payload = frame
            if msg_id == MSG_COMMAND_ACK:
                acks.append(decode_command_ack(payload))
        elif tag in ("CYCLE", "END"):
            events.append(Event(tag, "", _kv(parts[1:])))
        elif tag == "EVENT":
            events.append(Event(tag, parts[1], _kv(parts[2:])))
        else:
            die(f"unrecognized shim output line: '{line}'")
    return events, acks


def replay_fault_set(events: list) -> set:
    """Final latched-fault set, replayed from the EVENT fault edges."""
    latched = set()
    for e in events:
        if e.tag == "EVENT" and e.kind == "fault":
            if e.fields["edge"] == "latch":
                latched.add(e.fields["fault"])
            else:
                latched.discard(e.fields["fault"])
    return latched


def grade(expect: dict, events: list, acks: list) -> list:
    """Compare observed against expected; one Check per expectation, in scenario order."""
    checks = []

    # mode_log: each expected row must match an observed EVENT mode line, in order -
    # field-by-field, so SAFE entered for the wrong reason (wrong trigger/req) fails
    observed_rows = [e.fields for e in events if e.tag == "EVENT" and e.kind == "mode"]
    next_row = 0
    for i, want in enumerate(expect.get("mode_log", [])):
        hit = None
        for j in range(next_row, len(observed_rows)):
            if all(str(v) == observed_rows[j].get(k) for k, v in want.items()):
                hit = j
                break
        checks.append(
            Check(
                name=f"mode_log[{i}]",
                expected=str(want),
                observed=str(observed_rows[hit]) if hit is not None else "no matching transition",
                passed=hit is not None,
            )
        )
        if hit is not None:
            next_row = hit + 1  # later expectations must match later transitions

    # acks: graded against the decoded CommandAck frames (the telemetry emission is the
    # evidence, REQ-CMD-003) - matched by seq, the ground's correlation key
    for want in expect.get("acks", []):
        if "seq" not in want:
            die(f"ack expectation needs a seq: {want}")
        got = next((a for a in acks if a["seq"] == want["seq"]), None)
        wanted = {k: v for k, v in want.items() if k != "seq"}
        ok = got is not None and all(got.get(k) == v for k, v in wanted.items())
        checks.append(
            Check(
                name=f"ack[seq={want['seq']}]",
                expected=str(want),
                observed=str(got) if got is not None else "no ack with that seq",
                passed=ok,
            )
        )

    # final state: the END line plus the fault set replayed from the event edges
    final = expect.get("final", {})
    ends = [e for e in events if e.tag == "END"]
    if len(ends) != 1:
        die(f"expected exactly one END line, saw {len(ends)}")
    end = ends[0].fields
    if "mode" in final:
        checks.append(
            Check(
                name="final.mode",
                expected=str(final["mode"]),
                observed=end.get("mode", "missing"),
                passed=str(final["mode"]) == end.get("mode"),
            )
        )
    if "faults_set" in final:
        want_set = set(final["faults_set"])
        got_set = replay_fault_set(events)
        checks.append(
            Check(
                name="final.faults_set",
                expected=str(sorted(want_set)),
                observed=str(sorted(got_set)),
                passed=want_set == got_set,
            )
        )

    return checks


def expand_scenarios(paths: list) -> list:
    """Scenario files and/or directories -> a flat, sorted list of yaml files."""
    files = []
    for p in (Path(a) for a in paths):
        if p.is_dir():
            found = sorted(p.glob("*.yaml"))
            if not found:
                die(f"no scenario yaml files in {p}")
            files += found
        else:
            files.append(p)
    return files


def run_scenario(path: Path, verbose: bool) -> int:
    """One scenario end to end; prints the verdict, returns the exit code for it."""
    scenario = load_scenario(path)
    timeline_text = compile_timeline(scenario.timeline)
    stdout = run_shim(timeline_text)
    events, acks = parse_output(stdout)
    checks = grade(scenario.expect, events, acks)
    meta = [
        f"- Scenario: `{scenario.path.as_posix()}`",
        f"- Verifies: {', '.join(scenario.verifies) if scenario.verifies else '(none listed)'}",
    ]
    evidence = [("Injected timeline", timeline_text), ("Shim output", stdout)]
    report = write_report(scenario.id, scenario.title, meta, checks, evidence, "sil")
    return print_verdict(scenario.id, scenario.title, checks, report, verbose)


def main() -> int:
    ap = argparse.ArgumentParser(description="run SIL scenarios against the flight software")
    ap.add_argument("scenarios", nargs="+", help="scenario yaml files and/or directories of them")
    ap.add_argument(
        "--compile-only",
        action="store_true",
        help="print the compiled shim timeline instead of running (pipe it to the shim by hand)",
    )
    ap.add_argument(
        "-v", "--verbose", action="store_true", help="print every check, not just failures"
    )
    args = ap.parse_args()
    files = expand_scenarios(args.scenarios)

    if args.compile_only:
        if len(files) != 1:
            die("--compile-only takes exactly one scenario")
        print(compile_timeline(load_scenario(files[0]).timeline), end="")
        return EXIT_PASS

    results = [run_scenario(f, args.verbose) for f in files]
    if len(files) > 1:
        passed = sum(1 for r in results if r == EXIT_PASS)
        print(f"suite: {passed}/{len(files)} scenarios passed (reports in docs/reports/sil/)")
    return max(results)


if __name__ == "__main__":
    sys.exit(main())
