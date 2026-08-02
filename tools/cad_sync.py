#!/usr/bin/env python3
"""Sync a Fusion 360 STEP export into cad/ without churning unchanged parts.

Fusion stamps every export with a fresh time_stamp in the STEP header, so a byte compare says
every file changed on every export. The geometry lives in the DATA section; this compares only
that, with trailing whitespace normalized (pre-commit strips it from committed files), and copies
a file only when the part actually changed.

    python tools/cad_sync.py "%USERPROFILE%/Desktop/Fusion360Export"
    python tools/cad_sync.py "%USERPROFILE%/Desktop/Fusion360Export" --dry-run

Files in cad/ with no counterpart in the export are reported as STRAY but never deleted -
a rename in Fusion looks like a new file plus a stray, and deciding that is a human call.
"""

import argparse
import shutil
import sys

from pathlib import Path

REPO_CAD = Path(__file__).resolve().parent.parent / "cad"


def data_section(path: Path) -> list[str]:
    """The DATA section of a STEP file, one rstripped line per entry."""
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    try:
        start = next(i for i, ln in enumerate(lines) if ln.strip() == "DATA;")
    except StopIteration:
        # no DATA marker means not a well-formed STEP file; compare the whole thing
        start = 0
    return [ln.rstrip() for ln in lines[start:]]


def main() -> int:
    ap = argparse.ArgumentParser(description="sync fusion step exports into cad/")
    ap.add_argument("export_dir", type=Path, help="the folder fusion exported into")
    ap.add_argument("--dry-run", action="store_true", help="report only, copy nothing")
    args = ap.parse_args()

    if not args.export_dir.is_dir():
        print(f"not a directory: {args.export_dir}", file=sys.stderr)
        return 1

    exported = sorted(args.export_dir.rglob("*.step"))
    if not exported:
        print(f"no .step files under {args.export_dir}", file=sys.stderr)
        return 1

    new: list[Path] = []
    changed: list[Path] = []
    same = 0

    for src in exported:
        rel = src.relative_to(args.export_dir)
        dst = REPO_CAD / rel
        if not dst.exists():
            new.append(rel)
        elif data_section(src) != data_section(dst):
            changed.append(rel)
        else:
            same += 1
            continue
        if not args.dry_run:
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)

    seen = {src.relative_to(args.export_dir) for src in exported}
    stray = sorted(
        rel
        for rel in (p.relative_to(REPO_CAD) for p in REPO_CAD.rglob("*.step"))
        if rel not in seen
    )

    verb = "would copy" if args.dry_run else "copied"
    for rel in new:
        print(f"NEW      {rel}  ({verb})")
    for rel in changed:
        print(f"CHANGED  {rel}  ({verb})")
    for rel in stray:
        print(f"STRAY    {rel}  (in cad/ but not in the export - renamed or removed in fusion?)")
    print(f"{len(new)} new, {len(changed)} changed, {same} unchanged, {len(stray)} stray")
    return 0


if __name__ == "__main__":
    sys.exit(main())
