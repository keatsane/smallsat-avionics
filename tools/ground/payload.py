"""Reassembling a downlinked image from its chunks.

Each chunk says which image it belongs to, its index, and how many there are, so this needs no
state from anywhere else: chunks may arrive out of order, a pass may end halfway through, and a
later pass can finish the same image. That is the point of a store-and-forward downlink, and it
is why the assembler is written around a dict of chunks rather than an append-only buffer.
"""

import re
from collections import deque
from pathlib import Path

IMAGE_NAME = re.compile(r"^image_(\d+)\.jpg$")


def next_index(out_dir: Path) -> int:
    """One past the highest image number already in the directory, or 1 if it is empty.

    Highest rather than a count, so deleting the middle of a set does not hand out a number that
    is already taken.
    """
    highest = 0
    if out_dir.is_dir():
        for entry in out_dir.iterdir():
            m = IMAGE_NAME.match(entry.name)
            if m:
                highest = max(highest, int(m.group(1)))
    return highest + 1


class Image:
    """Chunks of one image, and whether they add up to all of it yet."""

    def __init__(self, image_id: int, chunks: int):
        self.image_id = image_id
        self.chunks = chunks
        self.parts: dict[int, bytes] = {}

    @property
    def complete(self) -> bool:
        return len(self.parts) == self.chunks

    @property
    def missing(self) -> list[int]:
        """Indices not yet received, in order - what a resume request would ask for."""
        return [i for i in range(self.chunks) if i not in self.parts]

    def data(self) -> bytes:
        return b"".join(self.parts[i] for i in sorted(self.parts))


class Assembler:
    """Feed it decoded payload_data_t dicts; it hands back images as they complete."""

    def __init__(self, out_dir: Path | None = None):
        self.out_dir = out_dir
        self.images: dict[int, Image] = {}

        # (image_id, chunk count) pairs already completed, so a repeat pass does not produce a
        # second identical file. the satellite sends every image three times because the link is
        # lossy and one-way, and the later passes exist to fill gaps - once there are none left to
        # fill they are the same picture arriving again.
        #
        # bounded rather than kept forever, because image ids restart at 1 when the obc reboots. a
        # few entries is enough to cover the passes of one image while being far too short to
        # still be holding an id from before a reset
        self.done: deque[tuple[int, int]] = deque(maxlen=4)

    def push(self, d: dict) -> tuple[Image | None, str]:
        """Take one chunk. Returns (completed image or None, a one-line progress note)."""
        if (d["image_id"], d["chunks"]) in self.done:
            return None, ""  # a repeat pass of a finished image says nothing worth printing

        img = self.images.get(d["image_id"])
        if img is None or img.chunks != d["chunks"]:
            img = Image(d["image_id"], d["chunks"])
            self.images[d["image_id"]] = img

        duplicate = d["chunk"] in img.parts
        img.parts[d["chunk"]] = d["data"]

        if not img.complete:
            note = f"image {img.image_id}: {len(img.parts)}/{img.chunks} chunks"
            return None, note + (" (duplicate)" if duplicate else "")

        del self.images[img.image_id]
        self.done.append((img.image_id, img.chunks))
        return img, f"image {img.image_id}: complete, {len(img.data())} bytes"

    def pending(self) -> tuple[int, int, list[int]] | None:
        """The incomplete image most worth chasing: (image_id, chunks, missing indices).

        The newest id, because an older incomplete image is one whose frame the vehicle has
        already overwritten - requesting its chunks would be asking for data that no longer
        exists anywhere.
        """
        if not self.images:
            return None
        image_id = max(self.images)
        img = self.images[image_id]
        missing = [i for i in range(img.chunks) if i not in img.parts]
        return (image_id, img.chunks, missing) if missing else None

    def save(self, img: Image) -> Path:
        """Write a completed image out. JPEG is what the sensor produces, so that is the suffix.

        Numbered from what is already on disk rather than from the image id on the wire. The two
        answer different questions: the wire id tells a receiver which chunks belong together and
        restarts at 1 every time the OBC reboots, while the filename is an archive label that
        should not collide with or skip past what is already saved.
        """
        self.out_dir.mkdir(parents=True, exist_ok=True)
        path = self.out_dir / f"image_{next_index(self.out_dir):04d}.jpg"
        path.write_bytes(img.data())
        return path


def looks_like_jpeg(data: bytes) -> bool:
    """SOI at the front, EOI at the back - the same check the firmware's bring-up makes."""
    return len(data) >= 4 and data[:2] == b"\xff\xd8" and data[-2:] == b"\xff\xd9"
