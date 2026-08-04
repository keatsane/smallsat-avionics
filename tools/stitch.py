#!/usr/bin/env python3
"""Join a survey's frames into one wide image - the capstone's last step, on the ground.

    python tools/stitch.py captures/image_0021.jpg captures/image_0022.jpg ...
    just stitch captures/image_002*.jpg

What this does and does not do is worth being exact about, because "panorama" implies more than
is happening here. The frames are laid side by side in the order given, which is the order the
survey flew them, and that is all. There is no feature matching, no overlap detection and no seam
blending: the camera's field of view is not calibrated on this build, so the true overlap between
adjacent frames is unknown and any blend would be invention rather than measurement.

What makes the result worth having is not the seams. It is that producing it required the vehicle
to hold a commanded bearing, take a frame, downlink it over a lossy radio with selective repeat,
slew to the next bearing and do it again - so a strip that lines up at all is evidence the whole
stack worked, several times in a row, unattended.
"""

import argparse
import sys

from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description="join survey frames into one wide image")
    ap.add_argument("frames", nargs="+", help="the downlinked jpegs, in bearing order")
    ap.add_argument("--out", default="captures/panorama.jpg", help="where to write it")
    ap.add_argument("--gap", type=int, default=4, help="pixels of margin between frames")
    args = ap.parse_args()

    from PIL import Image  # local: only the stitch path needs an imaging library

    images = []
    for name in args.frames:
        path = Path(name)
        if not path.exists():
            raise SystemExit(f"no such frame: {path}")
        images.append(Image.open(path).convert("RGB"))

    # a survey can be flown at one resolution and re-flown at another; matching heights keeps a
    # mixed set from producing a staircase
    height = min(im.height for im in images)
    scaled = [
        im if im.height == height else im.resize((round(im.width * height / im.height), height))
        for im in images
    ]

    width = sum(im.width for im in scaled) + args.gap * (len(scaled) - 1)
    strip = Image.new("RGB", (width, height), (16, 16, 16))
    x = 0
    for im in scaled:
        strip.paste(im, (x, 0))
        x += im.width + args.gap

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    strip.save(out, quality=92)
    print(f"{len(scaled)} frames -> {out} ({width}x{height})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
