"""Check the requirement trace both ways (REQ-VV-004).

REQ-VV-004 asks that every requirement trace forward to a verifying artifact, that code and tests
trace back to a requirement id, and - the part that actually decays - that the trace stay current.
At sixty requirements that is not something a person can re-check by hand after every change, so
it stops being checked at all and the document quietly drifts from the repo.

Two directions, because they catch different rot:

  forward   a requirement claiming verification names an artifact, and that file exists.
            catches evidence deleted, moved, or renamed out from under a claim.

  backward  a REQ id cited anywhere in the tree exists in requirements.md.
            catches typos, and ids left behind after a requirement is renumbered or dropped.

Prose artifacts ("this document", "obc SysTick driver") are real answers for requirements whose
evidence is not a file, so they are reported as unchecked rather than failed - the report says
what it could not verify instead of pretending it did.

    just trace          the report
    just trace --quiet  violations only
"""

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
REQUIREMENTS = REPO_ROOT / "docs" / "requirements.md"

REQ_ID = re.compile(r"REQ-[A-Z]+-\d{3}")
REQ_HEADING = re.compile(r"^\*\*(REQ-[A-Z]+-\d{3})\*\*")
FIELD = re.compile(r"^\*\*(Type|Status|Verification|Artifact)\*\*:\s*(.*?)\s*$")
# a repo-relative path: at least one directory segment, then a filename. loose on purpose - it is
# checked against the filesystem straight after, so a false positive fails loudly rather than
# passing quietly
PATH_TOKEN = re.compile(r"(?:[\w.-]+/)+[\w.-]+")

# where a REQ id may legitimately appear. vendored code is not ours to annotate, and the
# requirements document is the definition rather than a citation of it
BACKWARD_SCAN = ("common", "fsw", "obc", "tools")
BACKWARD_SUFFIXES = {".c", ".h", ".cpp", ".hpp", ".py", ".yaml", ".yml"}
SKIP_DIRS = {"vendor", "third_party", "build", "build-arm", "Debug", "__pycache__", ".pio"}


@dataclass
class Requirement:
    """One requirement block, as written in the document."""

    req_id: str
    line: int
    status: str = ""
    verification: str = ""
    artifacts: list[str] = field(default_factory=list)

    @property
    def claims_verification(self) -> bool:
        """True when the status asserts evidence exists, so an artifact is owed."""
        return "verified" in self.status.lower()


def split_artifacts(raw: str) -> list[str]:
    """One Artifact line -> its entries, splitting only outside parentheses.

    A parenthetical can hold both separators and paths - "dual UART (USART2 console, USART6
    downlink)" is one artifact whose comma means nothing, while "(SIL backend in
    fsw/sil/sil_shim.cpp, STM32 backend in fsw/platform/stm32/platform_stm32.cpp)" names two real
    files. Splitting naively breaks the first; stripping parentheticals first loses the second.
    Tracking depth keeps both.
    """
    entries: list[str] = []
    current: list[str] = []
    depth = 0

    for char in raw:
        if char == "(":
            depth += 1
        elif char == ")":
            depth = max(0, depth - 1)

        if char in ",;" and depth == 0:
            entries.append("".join(current))
            current = []
        else:
            current.append(char)

    entries.append("".join(current))
    return [entry.strip() for entry in entries if entry.strip()]


def find_paths(entry: str) -> list[str]:
    """Every repo-relative path an artifact entry names.

    Pulled out of the entry rather than matched against the whole of it, because plenty of them
    are a sentence with a path inside - "the task table in obc/Inc/rtos_tasks.h" is a real
    artifact and the file is the part worth checking. Requiring the entry to be nothing but a path
    would silently skip those, which is the opposite of what this script is for.
    """
    return PATH_TOKEN.findall(entry.replace("`", ""))


def parse_requirements(path: Path) -> list[Requirement]:
    """Read the document into requirement blocks, in document order."""
    reqs: list[Requirement] = []
    for number, text in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        heading = REQ_HEADING.match(text)
        if heading:
            reqs.append(Requirement(req_id=heading.group(1), line=number))
            continue

        field_match = FIELD.match(text)
        if not field_match or not reqs:
            continue
        name, value = field_match.group(1), field_match.group(2)
        if name == "Status":
            reqs[-1].status = value
        elif name == "Verification":
            reqs[-1].verification = value
        elif name == "Artifact":
            reqs[-1].artifacts = split_artifacts(value)

    return reqs


def scan_for_ids(root: Path) -> dict[str, list[str]]:
    """Every REQ id cited in the tree -> the repo-relative files citing it."""
    cited: dict[str, list[str]] = {}
    for directory in BACKWARD_SCAN:
        if not (root / directory).is_dir():
            continue
        for file in sorted((root / directory).rglob("*")):
            if file.suffix not in BACKWARD_SUFFIXES:
                continue
            if any(part in SKIP_DIRS for part in file.parts):
                continue
            try:
                text = file.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            where = file.relative_to(root).as_posix()
            for req_id in sorted(set(REQ_ID.findall(text))):
                cited.setdefault(req_id, []).append(where)
    return cited


def check(
    root: Path = REPO_ROOT, requirements: Path | None = None
) -> tuple[list[str], list[str], list[Requirement]]:
    """Run both directions. Returns (violations, unchecked prose notes, requirements).

    Both paths are arguments so the tests can point it at a synthetic tree - a checker that has
    only ever been run against a passing repo has not been shown to catch anything.
    """
    reqs = parse_requirements(requirements or REQUIREMENTS)
    violations: list[str] = []
    unchecked: list[str] = []
    known = {r.req_id for r in reqs}

    for req in reqs:
        if req.claims_verification and not req.artifacts:
            violations.append(
                f"{req.req_id} (line {req.line}): status claims verification but names no artifact"
            )

        for entry in req.artifacts:
            paths = find_paths(entry)
            if not paths:
                unchecked.append(f"{req.req_id}: {entry}")
            for path in paths:
                if not (root / path).exists():
                    violations.append(
                        f"{req.req_id} (line {req.line}): artifact not found - {path}"
                    )

    for req_id, files in sorted(scan_for_ids(root).items()):
        if req_id not in known:
            violations.append(f"{req_id}: cited in {', '.join(files)} but not in requirements.md")

    return violations, unchecked, reqs


def main() -> int:
    ap = argparse.ArgumentParser(description="check the requirement trace both ways (REQ-VV-004)")
    ap.add_argument("--quiet", action="store_true", help="print violations only")
    args = ap.parse_args()

    violations, unchecked, reqs = check()

    if not args.quiet:
        verified = sum(1 for r in reqs if r.claims_verification)
        checked = sum(len(find_paths(e)) for r in reqs for e in r.artifacts)
        print(f"{len(reqs)} requirements, {verified} claiming verification")
        print(f"{checked} artifact paths checked, {len(unchecked)} entries not a path")
        for note in unchecked:
            print(f"  unchecked: {note}")

    for violation in violations:
        print(f"FAIL {violation}")

    if violations:
        print(f"\n{len(violations)} trace violation(s)")
        return 1
    if not args.quiet:
        print("\ntrace is current")
    return 0


if __name__ == "__main__":
    sys.exit(main())
