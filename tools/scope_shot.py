#!/usr/bin/env python3
"""Save the bench scope's screen over USB (Siglent SDS, SCDP command).

Run: python tools/scope_shot.py docs/reports/img/HIL-001-period.png
Needs pyvisa (pip install pyvisa) plus a VISA backend for USBTMC on windows -
the NI-VISA runtime works. Scope's rear USB Device port to the PC.
"""

import argparse
import sys
import warnings
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description="save the scope's screen over usb (siglent sds)")
    ap.add_argument("out", help="output image, e.g. docs/reports/img/HIL-001-period.png")
    ap.add_argument(
        "--resource", default=None, help="visa resource string (default: first USB instrument)"
    )
    args = ap.parse_args()

    import pyvisa  # lazy: bench-only dep
    from pyvisa.resources import MessageBasedResource

    # pyvisa-py warns its LAN discovery is limited without psutil/zeroconf - we only
    # talk usb, so silence the advice instead of installing network-scan deps
    warnings.filterwarnings("ignore", module=r"pyvisa_py(\..*)?")

    # default backend (ni-visa etc.) when present, else the pure-python pyvisa-py one
    try:
        rm = pyvisa.ResourceManager()
    except (ValueError, OSError):
        rm = pyvisa.ResourceManager("@py")
    resource = args.resource
    if resource is None:
        usb = [r for r in rm.list_resources() if r.startswith("USB")]
        if not usb:
            print(
                "scope_shot: no USB VISA instrument found (rear USB Device port?)", file=sys.stderr
            )
            return 1
        resource = usb[0]

    scope = rm.open_resource(resource)
    # open_resource is typed as the base Resource; a usb scope is message-based, so narrow it -
    # that gives the type checker timeout/chunk_size/query/write/read_raw and is a real sanity check
    assert isinstance(scope, MessageBasedResource)
    scope.timeout = 10000  # ms - the image takes a moment
    scope.chunk_size = 1024 * 1024  # the dump is one big blob; the 20 KB default truncates it
    print(f"connected: {scope.query('*IDN?').strip()}")
    scope.write("SCDP")
    data = scope.read_raw()
    # read_raw can return a single chunk of the dump; a bmp header carries the true
    # file size, so keep reading until it's all here
    if data.startswith(b"BM") and len(data) >= 6:
        total = int.from_bytes(data[2:6], "little")
        while len(data) < total:
            data += scope.read_raw()

    # the scope only speaks bmp; convert to the asked-for png (pillow), else save what came
    out = Path(args.out)
    kind = "png" if data.startswith(b"\x89PNG") else "bmp" if data.startswith(b"BM") else None
    out.parent.mkdir(parents=True, exist_ok=True)
    if kind == "bmp" and out.suffix.lower() == ".png":
        import io

        from PIL import Image  # pillow: pip install pillow

        Image.open(io.BytesIO(data)).save(out)
        print(f"saved {out.as_posix()} (converted from the scope's bmp)")
        return 0
    if kind and out.suffix.lower() != f".{kind}":
        out = out.with_suffix(f".{kind}")
        print(f"scope sent {kind}, saving as {out.name}")
    out.write_bytes(data)
    print(f"saved {len(data)} bytes -> {out.as_posix()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
