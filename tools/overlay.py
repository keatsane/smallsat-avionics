#!/usr/bin/env python3
"""Lay a bench run over the same manoeuvre in the plant model - Phase 7's closing artifact.

A SIL harness that agrees with itself proves nothing. The point of modelling this vehicle was to
predict what the hardware would do, and the only way to know whether it does is to run the same
controller against both and put the two rate curves on one pair of axes.

    just overlay                                   # the recorded reference run
    python tools/overlay.py --rig docs/data/my.csv --scenario SIL-011

The rig side is a csv from `uart_monitor.py --record`, which stamps every attitude frame with the
vehicle's own clock. The sim side is a SIL scenario, re-run here so the comparison is against
today's model rather than a number copied out of an old report.

What it prints is the number that matters: the deceleration each side achieved. A plot that looks
close is not evidence; two figures that agree, or do not, is.
"""

import argparse
import csv
import subprocess
import sys

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SHIM = ROOT / "fsw" / "build" / "sil_shim.exe"
SCENARIOS = ROOT / "fsw" / "sil" / "scenarios"

# a rate this small is the platform at rest as far as this comparison is concerned - below the
# controller's own deadband, and inside the gyro's noise once the bias is subtracted
AT_REST_DPS = 3.0


def load_rig(path: Path, rate_col: str = "rate_dps"):
    """(seconds, deg/s) from a --record csv, with time re-zeroed to the first sample."""
    ts, rates = [], []
    with path.open(encoding="utf-8") as fp:
        for row in csv.DictReader(fp):
            ts.append(int(row["t_ms"]) / 1000.0)
            rates.append(float(row[rate_col]))
    if not ts:
        raise SystemExit(f"{path} has no samples")
    t0 = ts[0]
    return [t - t0 for t in ts], rates


def run_scenario(scenario_id: str):
    """(seconds, deg/s) from re-running a SIL scenario against the current plant model."""
    import yaml  # local: the plotting path should not need the scenario stack to import

    matches = list(SCENARIOS.glob(f"*{scenario_id.lower().replace('-', '_')}*.yaml"))
    if not matches:
        raise SystemExit(f"no scenario file matching {scenario_id} under {SCENARIOS}")
    spec = yaml.safe_load(matches[0].read_text(encoding="utf-8"))

    # rebuild the shim's stdin the way the runner does - this tool grades nothing, it only wants
    # the plant trace, so it needs the timeline and none of the expectations
    lines = []
    for step in spec["timeline"]:
        parts = [str(step["t"])]
        if "plant" in step:
            parts += ["plant", str(step["plant"])]
        if "nudge" in step:
            parts += ["nudge", str(step["nudge"])]
        if "cmd" in step:
            c = step["cmd"]
            parts += ["cmd", c["name"], str(c["arg"]), str(c["seq"])]
        lines.append(" ".join(parts))

    if not SHIM.exists():
        raise SystemExit(f"{SHIM} not built - run `just fsw-build` first")
    out = subprocess.run(
        [str(SHIM)], input="\n".join(lines) + "\n", capture_output=True, text=True, timeout=30
    ).stdout

    # PLANT lines carry the state and the CYCLE line that follows carries its time
    ts, rates, pending = [], [], None
    for line in out.splitlines():
        if line.startswith("PLANT "):
            fields = dict(p.split("=", 1) for p in line.split()[1:])
            pending = float(fields["rate"])
        elif line.startswith("CYCLE ") and pending is not None:
            ts.append(int(line.split("t=")[1]) / 1000.0)
            rates.append(pending * 57.29577951)  # rad/s -> deg/s, the rig's unit
            pending = None
    return ts, rates


def decay_rate(ts, rates):
    """Mean deceleration in deg/s^2 from the peak until the decay ends.

    "Ends" is whichever comes first: the platform reaching rest, or the rate changing sign. The
    sign test is what makes this usable on hand-spun bench data - a platform that swings through
    zero and back out has been disturbed again, and averaging across that measures the hand
    rather than the plant.
    """
    mags = [abs(r) for r in rates]
    peak_i = mags.index(max(mags))
    sign = 1.0 if rates[peak_i] >= 0 else -1.0

    end_i = len(mags) - 1
    for i in range(peak_i + 1, len(mags)):
        if (rates[i] * sign) < 0.0:
            end_i = i - 1  # the swing back through zero is a new disturbance, not the decay
            break
        if mags[i] < AT_REST_DPS:
            end_i = i  # at rest, and this sample is the end of the arrest rather than after it
            break

    # then back up to where the slowing actually started. a hand-spun run has a plateau - the
    # platform held at speed while it was still being pushed - and averaging the plateau in with
    # the arrest halves the answer. the arrest is the last unbroken stretch of falling magnitude,
    # so walk back from the end while each earlier sample is faster than the one after it
    start_i = end_i
    while start_i > 0 and mags[start_i - 1] > mags[start_i]:
        start_i -= 1

    dt = ts[end_i] - ts[start_i]
    drop = mags[start_i] - mags[end_i]
    return (drop / dt if dt > 0 else 0.0), ts[start_i], ts[end_i]


def main() -> int:
    ap = argparse.ArgumentParser(description="overlay a bench run on the plant model")
    ap.add_argument(
        "--rig",
        default="docs/data/rig_detumble_2026-08-04.csv",
        help="csv from uart_monitor.py --record",
    )
    ap.add_argument("--scenario", default="SIL-011", help="scenario to re-run for the sim curve")
    ap.add_argument(
        "--out", default="docs/reports/overlay_coast.png", help="where to write the plot"
    )
    args = ap.parse_args()

    rig_t, rig_r = load_rig(Path(args.rig))
    sim_t, sim_r = run_scenario(args.scenario)

    rig_d, rig_a, rig_b = decay_rate(rig_t, rig_r)
    sim_d, sim_a, sim_b = decay_rate(sim_t, sim_r)

    print(f"rig  {Path(args.rig).name}: {rig_d:7.1f} deg/s^2  ({rig_a:.1f}s -> {rig_b:.1f}s)")
    print(f"sim  {args.scenario}:      {sim_d:7.1f} deg/s^2  ({sim_a:.1f}s -> {sim_b:.1f}s)")
    if sim_d > 0:
        print(f"ratio rig/sim: {rig_d / sim_d:.2f}")

    import matplotlib

    matplotlib.use("Agg")  # a bench tool writes files; it never owns a window
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(8, 4.5))
    ax.plot(sim_t, [abs(r) for r in sim_r], label=f"plant model ({args.scenario})", linewidth=2)
    ax.plot(rig_t, [abs(r) for r in rig_r], label="rig", linewidth=2, marker="o", markersize=3)
    ax.set_xlabel("seconds")
    ax.set_ylabel("body rate, deg/s (magnitude)")
    ax.set_title(f"{args.scenario}: plant model against the rig")
    ax.grid(alpha=0.3)
    ax.legend()
    fig.tight_layout()

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, dpi=130)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
