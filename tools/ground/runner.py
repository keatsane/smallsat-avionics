"""Shared harness core for the scenario runners.

What every runner has in common lives here: the pass/fail/error exit codes, the
Check record, the markdown report writer (REQ-VV-002), and the console verdict
printer. The level-specific halves stay in their own files - sil_runner.py drives
the shim from a YAML timeline, hil_runner.py watches a live serial port.
"""

import sys
from dataclasses import dataclass
from pathlib import Path
from typing import NoReturn

# parents: [0] ground/ [1] tools/ [2] the repo root
REPO_ROOT = Path(__file__).resolve().parents[2]
REPORT_DIR = REPO_ROOT / "docs" / "reports"

# exit codes: a failed scenario and a broken harness are different news
EXIT_PASS = 0
EXIT_FAIL = 1
EXIT_ERROR = 2


def die(msg: str) -> NoReturn:
    """Harness error - not a scenario verdict. One line to stderr, exit 2."""
    prog = Path(sys.argv[0]).stem or "runner"
    print(f"{prog}: {msg}", file=sys.stderr)
    sys.exit(EXIT_ERROR)


@dataclass
class Check:
    """One graded expectation - a row of the report table."""

    name: str
    expected: str
    observed: str
    passed: bool


def resolve_scenarios(refs: list, scenario_dir: Path, prefix: str) -> list:
    """Scenario references -> a deduped, ordered list of yaml paths.

    A reference is "all" (every scenario in the level's directory), an existing
    file or directory path, a bare number (5 -> <prefix>_005_*.yaml), or a name
    fragment (command_loss). No match is a harness error - it dies loudly with
    the available ids rather than silently running nothing.
    """
    files = []
    for ref in refs:
        path = Path(ref)
        if ref == "all":
            hits = sorted(scenario_dir.glob("*.yaml"))
        elif path.is_file():
            hits = [path]
        elif path.is_dir():
            hits = sorted(path.glob("*.yaml"))
        elif ref.isdigit():
            hits = sorted(scenario_dir.glob(f"{prefix}_{int(ref):03d}_*.yaml"))
        else:
            hits = sorted(scenario_dir.glob(f"{prefix}_*{ref}*.yaml"))
        if not hits:
            available = ", ".join(f.stem for f in sorted(scenario_dir.glob("*.yaml"))) or "(none)"
            die(f"no scenario matches '{ref}' (available: {available})")
        files += hits
    seen = set()
    return [f for f in files if not (f in seen or seen.add(f))]


def repo_relative(path: Path) -> Path:
    """Repo-relative form of a path for display in reports - a checked-in report must
    regenerate byte-identical on any machine, so absolute paths never land in one."""
    resolved = Path(path).resolve()
    return resolved.relative_to(REPO_ROOT) if resolved.is_relative_to(REPO_ROOT) else path


def write_report(
    report_id: str, title: str, meta_lines: list, checks: list, evidence: list, subdir: str
) -> Path:
    """Render the markdown report (REQ-VV-002) and return its path.

    meta_lines are the bullet lines under the verdict (scenario path, verifies, ...);
    evidence is a list of (heading, text) sections appended verbatim in code blocks;
    subdir is the per-level home under docs/reports/ ("sil", "hil").
    """
    passed = all(c.passed for c in checks)
    # no timestamp on purpose: a rerun with the same result must be byte-identical, so the
    # report only diffs when the verdict or evidence actually changes
    lines = [
        f"# {report_id} - {title}",
        "",
        f"**Verdict: {'PASS' if passed else 'FAIL'}**",
        "",
        *meta_lines,
        f"- Checks: {sum(c.passed for c in checks)}/{len(checks)} passed",
        "",
        "## Checks",
        "",
        "| Check | Expected | Observed | Result |",
        "| ----- | -------- | -------- | ------ |",
    ]
    for c in checks:
        result = "pass" if c.passed else "FAIL"
        lines.append(f"| {c.name} | `{c.expected}` | `{c.observed}` | {result} |")
    for heading, text in evidence:
        lines += ["", f"## {heading}", "", "```", text.rstrip(), "```"]
    lines.append("")
    out_dir = REPORT_DIR / subdir
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{report_id}.md"
    # pin LF so reports regenerated on windows and linux CI are byte-identical
    out.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    return out


def print_verdict(
    report_id: str, title: str, checks: list, report_path: Path, verbose: bool
) -> int:
    """One PASS/FAIL summary line, detail only on failure or with -v; returns the exit code."""
    passed = all(c.passed for c in checks)
    summary = f"{sum(c.passed for c in checks)}/{len(checks)} checks"
    print(f"{'PASS' if passed else 'FAIL'}  {report_id}  {title} ({summary})")
    # clean summary by default; per-check detail only when it matters (a failure) or asked for
    for c in checks:
        if verbose or not c.passed:
            result = "pass" if c.passed else "FAIL"
            print(f"      {result}  {c.name}: expected {c.expected}, observed {c.observed}")
    if verbose or not passed:
        print(f"      report: {report_path.relative_to(REPO_ROOT).as_posix()}")
    return EXIT_PASS if passed else EXIT_FAIL
