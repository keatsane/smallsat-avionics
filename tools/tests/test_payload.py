"""Tests for image reassembly (ground/payload.py)."""

import struct

from ground.frames import MSG_PAYLOAD_DATA, PAYLOAD_CHUNK_BYTES, decode_payload_data, encode
from ground.payload import Assembler, looks_like_jpeg, next_index


def _chunk(image_id: int, chunk: int, chunks: int, data: bytes) -> dict:
    """Build a payload_data_t the way the firmware does, then decode it back."""
    body = struct.pack("<3H2B", image_id, chunk, chunks, len(data), 0)
    body += data.ljust(PAYLOAD_CHUNK_BYTES, b"\x00")
    return decode_payload_data(body)


def _split(data: bytes) -> list[bytes]:
    return [data[i : i + PAYLOAD_CHUNK_BYTES] for i in range(0, len(data), PAYLOAD_CHUNK_BYTES)]


JPEG = b"\xff\xd8" + bytes(range(256)) * 2 + b"\xff\xd9"


def test_chunk_roundtrips_through_a_frame():
    # what the firmware frames must decode to the same fields on the ground
    body = struct.pack("<3H2B", 7, 2, 9, 3, 0) + b"abc".ljust(PAYLOAD_CHUNK_BYTES, b"\x00")
    frame = encode(MSG_PAYLOAD_DATA, body)
    assert frame[2] == MSG_PAYLOAD_DATA
    d = decode_payload_data(body)
    assert d == {"image_id": 7, "chunk": 2, "chunks": 9, "len": 3, "data": b"abc"}


def test_in_order_reassembly_reproduces_the_image():
    parts = _split(JPEG)
    asm = Assembler()
    done = None
    for i, part in enumerate(parts):
        done, _ = asm.push(_chunk(1, i, len(parts), part))
    assert done is not None
    assert done.data() == JPEG
    assert looks_like_jpeg(done.data())


def test_out_of_order_reassembly_reproduces_the_image():
    # a real link reorders; the chunk index is what makes that survivable
    parts = _split(JPEG)
    asm = Assembler()
    order = list(range(len(parts)))
    order = order[1::2] + order[0::2]  # odds first, then evens
    done = None
    for i in order:
        done, _ = asm.push(_chunk(1, i, len(parts), parts[i]))
    assert done is not None
    assert done.data() == JPEG


def test_incomplete_image_is_not_returned_and_reports_what_is_missing():
    parts = _split(JPEG)
    asm = Assembler()
    for i, part in enumerate(parts):
        if i == 3:
            continue  # a pass that dropped one frame
        done, _ = asm.push(_chunk(1, i, len(parts), part))
        assert done is None
    assert asm.images[1].missing == [3]
    assert not asm.images[1].complete


def test_a_duplicate_chunk_does_not_complete_an_image():
    parts = _split(JPEG)
    asm = Assembler()
    for _ in range(len(parts)):
        done, note = asm.push(_chunk(1, 0, len(parts), parts[0]))
        assert done is None
    assert "duplicate" in note


def test_two_images_interleave_without_mixing():
    a, b = _split(JPEG), _split(JPEG[::-1])
    asm = Assembler()
    finished = []
    for i in range(max(len(a), len(b))):
        if i < len(a):
            done, _ = asm.push(_chunk(1, i, len(a), a[i]))
            if done:
                finished.append(done)
        if i < len(b):
            done, _ = asm.push(_chunk(2, i, len(b), b[i]))
            if done:
                finished.append(done)
    assert [f.image_id for f in finished] == [1, 2]
    assert finished[0].data() == JPEG
    assert finished[1].data() == JPEG[::-1]


def test_saved_file_is_byte_identical(tmp_path):
    parts = _split(JPEG)
    asm = Assembler(tmp_path)
    for i, part in enumerate(parts):
        done, _ = asm.push(_chunk(4, i, len(parts), part))
    path = asm.save(done)
    assert path.read_bytes() == JPEG
    # named from the directory, not from the wire id - an empty directory starts at 1
    assert path.name == "image_0001.jpg"


def test_filenames_follow_the_directory_not_the_wire_id(tmp_path):
    parts = _split(JPEG)
    asm = Assembler(tmp_path)

    # three images arrive with wire ids that have nothing to do with what is on disk
    for wire_id in (7, 8, 9):
        for i, part in enumerate(parts):
            done, _ = asm.push(_chunk(wire_id, i, len(parts), part))
        asm.save(done)

    assert sorted(p.name for p in tmp_path.iterdir()) == [
        "image_0001.jpg",
        "image_0002.jpg",
        "image_0003.jpg",
    ]


def test_next_index_uses_the_highest_not_the_count(tmp_path):
    assert next_index(tmp_path) == 1  # empty
    (tmp_path / "image_0001.jpg").write_bytes(b"x")
    (tmp_path / "image_0005.jpg").write_bytes(b"x")
    assert next_index(tmp_path) == 6  # not 3 - a deleted middle must not be reissued

    (tmp_path / "notes.txt").write_bytes(b"x")
    (tmp_path / "image_nope.jpg").write_bytes(b"x")
    assert next_index(tmp_path) == 6  # unrelated files ignored


def test_next_index_on_a_missing_directory(tmp_path):
    assert next_index(tmp_path / "not-created-yet") == 1


def test_looks_like_jpeg_rejects_truncation():
    assert looks_like_jpeg(JPEG)
    assert not looks_like_jpeg(JPEG[:-2])  # lost the EOI
    assert not looks_like_jpeg(JPEG[2:])  # lost the SOI
    assert not looks_like_jpeg(b"")


def test_a_repeated_pass_does_not_save_the_image_twice():
    # the satellite sends every image three times, because the payload link is one-way and lossy.
    # once the ground has all of it the later passes are the same picture again, and each one used
    # to land as its own file
    parts = _split(JPEG)
    asm = Assembler()
    finished = []
    for _ in range(3):
        for i, part in enumerate(parts):
            done, _ = asm.push(_chunk(1, i, len(parts), part))
            if done:
                finished.append(done)
    assert len(finished) == 1
    assert finished[0].data() == JPEG


def test_the_next_capture_still_lands_after_a_repeat():
    # the guard must key on the image, not simply refuse anything already seen
    parts = _split(JPEG)
    asm = Assembler()
    for _ in range(2):
        for i, part in enumerate(parts):
            asm.push(_chunk(1, i, len(parts), part))

    other = _split(JPEG[::-1])
    done = None
    for i, part in enumerate(other):
        got, _ = asm.push(_chunk(2, i, len(other), part))
        done = got or done
    assert done is not None
    assert done.data() == JPEG[::-1]


def test_pending_names_exactly_the_missing_chunks():
    # the ground half of selective repeat: after a lossy pass, this list is what goes back up
    parts = _split(JPEG)
    asm = Assembler()
    for i, part in enumerate(parts):
        if i in (1, 3):
            continue  # lost on the air
        asm.push(_chunk(5, i, len(parts), part))

    got = asm.pending()
    assert got is not None
    image_id, total, missing = got
    assert (image_id, total) == (5, len(parts))
    assert missing == [1, 3]

    # the resends arrive and the request list empties with them
    asm.push(_chunk(5, 1, len(parts), parts[1]))
    done, _ = asm.push(_chunk(5, 3, len(parts), parts[3]))
    assert done is not None
    assert asm.pending() is None
    assert done.data() == JPEG
