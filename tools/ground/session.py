"""The ground console's protocol brain, separated from its terminal.

Everything that used to live in closures inside uart_monitor.py's reader thread - command
sequencing, the retry machinery, pass accounting, selective-repeat requests, text salvage - is a
state machine over bytes and time, and none of it needs a serial port or a prompt to be true. So
it lives here as a class that takes bytes and a clock and returns display lines and bytes to
transmit, and the monitor shrinks to I/O around it.

The separation is what makes this testable: every link-layer bug this project has had lived in
exactly this logic, and while it sat inside `main()` not one of its behaviours could be pinned by
a test. Same reasoning as the HIL runner's link monitor, which takes time as an argument for the
same reason.

Thread contract: the monitor calls `command()` from the prompt thread and `feed()`/`tick()` from
the reader thread, so every public method takes the internal lock. Callers write the returned
bytes to the port themselves - the session never touches I/O.
"""

import threading

from ground.frames import (
    CHUNK_REQUEST_MAX,
    MSG_COMMAND_ACK,
    MSG_DOWNLINK_STATUS,
    MSG_GROUND_STATUS,
    MSG_PAYLOAD_DATA,
    FrameDecoder,
    decode_command_ack,
    decode_downlink_status,
    decode_ground_status,
    decode_payload_data,
    encode_chunk_request,
    encode_command,
    format_frame,
)
from ground.payload import Assembler, looks_like_jpeg

# a command is retransmitted until the vehicle acknowledges it. the lora link is half duplex and
# the vehicle is deaf for the ~57 ms its beacon is on the air, so a command sent at the wrong
# moment is simply not heard - on the bench that lost about one in six. the flight software drops
# a repeated sequence number rather than acting twice, so a resend costs nothing when the command
# did arrive and only the ack was lost.
#
# the interval is deliberately not a multiple of the 1 s beacon: a retry that keeps landing in the
# same relative slot would keep hitting the same deaf window forever
RETRY_AFTER_S = 0.7
RETRY_LIMIT = 4  # gives up after ~2.8 s, well inside the 5 s command-loss timeout

# selective repeat is asked for at most this often. NOT 1.0 seconds, and the difference is a
# bench story: requests used to fire on receipt of the vehicle's own 1 Hz status frame, which
# phase-locked them to the vehicle's transmit schedule - and on the bench eight consecutive
# requests launched into the exact window where the vehicle's radio was mid-transmit and deaf.
# a half-duplex link punishes anything synchronised to the other end's clock; 1.3 s drifts the
# phase by a third of a second every round, so no alignment survives
REQUEST_EVERY_S = 1.3

# how long after the last idle status frame the vehicle is still assumed to be in DOWNLINK and
# listening - requests stop when the vehicle leaves rather than being shouted into STANDBY
DOWNLINK_FRESH_S = 3.0

# a downlinking image is hundreds of frames; one line each would bury everything else, so progress
# is reported at intervals and on completion rather than per chunk
PAYLOAD_PROGRESS_EVERY = 25


class GroundSession:
    """Byte stream + clock in, display lines + uplink bytes out."""

    def __init__(self, assembler: Assembler, retry: bool = True, transmit: bool = True):
        self.assembler = assembler
        self.retry = retry  # False = every command is fire-and-forget (--no-retry)
        self.transmit = transmit  # False = never produce uplink bytes (--read-only)

        self.decoder = FrameDecoder()
        self.lock = threading.Lock()

        self.seq = 0
        self.outstanding: dict[int, dict] = {}  # seq -> frame, label, tries, due

        # pass accounting: what each end counted when the current downlink pass began, so its end
        # can be reported as a delivery rate rather than two numbers nobody wants to subtract
        self.pass_start: dict | None = None
        self.ground_now = {"packets": 0, "frames": 0}

        self.last_request = 0.0
        self.dl_idle_at = -1e9  # when the vehicle last said "in DOWNLINK, nothing in flight"
        self.text = bytearray()  # printable bytes between frames - firmware debug prints

    # ---- transmit side ----

    def command(self, cmd_id: int, arg: int, label: str, now: float, retry: bool = True):
        """Encode one command. Returns (display lines, bytes to transmit)."""
        with self.lock:
            self.seq = (self.seq + 1) & 0xFFFF
            frame = encode_command(cmd_id, arg, self.seq)
            if retry and self.retry:
                self.outstanding[self.seq] = {
                    "frame": frame,
                    "label": label,
                    "tries": 1,
                    "due": now + RETRY_AFTER_S,
                }
            return [f"{'SENT':<12} {label}  seq={self.seq}"], [frame]

    def tick(self, now: float):
        """Resend anything unacknowledged and overdue; say so when one is given up on. Also the
        clock for selective-repeat requests, precisely so they are NOT driven by the arrival of
        the vehicle's frames - see REQUEST_EVERY_S for what that synchronisation cost."""
        lines: list[str] = []
        tx: list[bytes] = []
        with self.lock:
            self._request_chunks(now, lines, tx)
            for s_no, p in list(self.outstanding.items()):
                if now < p["due"]:
                    continue
                if p["tries"] >= RETRY_LIMIT:
                    del self.outstanding[s_no]
                    lines.append(
                        f"{'LINKERR':<12} no ack for {p['label']}  seq={s_no} "
                        f"after {RETRY_LIMIT} tries"
                    )
                    continue
                p["tries"] += 1
                p["due"] = now + RETRY_AFTER_S
                tx.append(p["frame"])
                lines.append(f"{'SENT':<12} {p['label']}  seq={s_no}  (retry {p['tries']})")
        return lines, tx

    # ---- receive side ----

    def feed(self, data: bytes, now: float):
        """Run received bytes through the decoder. Returns (display lines, bytes to transmit)."""
        lines: list[str] = []
        tx: list[bytes] = []
        with self.lock:
            for byte in data:
                hunting = self.decoder.state == "sync0"
                crc_before = self.decoder.crc_errors
                frame = self.decoder.push(byte)
                # a corrupt frame was once counted and never reported, which made a link problem
                # look like stray debug text. say so the moment it happens
                if self.decoder.crc_errors != crc_before:
                    lines.append(
                        f"{'LINKERR':<12} bad crc - frames dropped={self.decoder.crc_errors}"
                    )
                if frame is not None:
                    self._handle_frame(frame, now, lines, tx)
                elif hunting and self.decoder.state == "sync0":
                    # stray byte outside any frame: debug text from the firmware
                    if byte == 0x0A:
                        self._flush_text(lines)
                    elif 0x20 <= byte < 0x7F:
                        self.text.append(byte)
        return lines, tx

    def idle(self):
        """The port went quiet - surface any half line of firmware text rather than sit on it."""
        lines: list[str] = []
        with self.lock:
            self._flush_text(lines)
        return lines

    # ---- internals, called with the lock held ----

    def _flush_text(self, lines: list) -> None:
        if self.text:
            lines.append(f"{'TEXT':<12} {self.text.decode('ascii')}")
            self.text.clear()

    def _handle_frame(self, frame, now: float, lines: list, tx: list) -> None:
        msg_id, payload = frame

        if msg_id == MSG_PAYLOAD_DATA:
            self._take_chunk(payload, lines)
            return

        if msg_id == MSG_COMMAND_ACK:
            self.outstanding.pop(decode_command_ack(payload)["seq"], None)
        elif msg_id == MSG_GROUND_STATUS:
            g = decode_ground_status(payload)
            self.ground_now["packets"] = g["nrf24_packets"]
            self.ground_now["frames"] = g["nrf24_frames"]
        elif msg_id == MSG_DOWNLINK_STATUS:
            d = decode_downlink_status(payload)
            self._note_pass(d, lines)
            if d["chunks"] == 0:
                self.dl_idle_at = now  # idle and listening - requests are worth sending
        lines.append(format_frame(msg_id, payload))

    def _take_chunk(self, payload: bytes, lines: list) -> None:
        """One image chunk: reassemble, report sparingly, write the file when it is whole."""
        d = decode_payload_data(payload)
        img, note = self.assembler.push(d)
        if img is None:
            if note and d["chunk"] % PAYLOAD_PROGRESS_EVERY == 0:
                lines.append(f"{'PAYLOAD':<12} {note}")
            return

        path = self.assembler.save(img)
        # the same SOI/EOI check the firmware makes, repeated at the other end of the link - the
        # one thing that proves the bytes survived framing, chunking, and reassembly intact
        verdict = "JPEG OK" if looks_like_jpeg(img.data()) else "BAD MARKERS"
        lines.append(f"{'PAYLOAD':<12} {note} -> {path} ({verdict})")

    def _note_pass(self, d: dict, lines: list) -> None:
        """Watch a downlink begin and end, and say what fraction of it arrived."""
        if d["chunks"] > 0:
            if self.pass_start is None:
                self.pass_start = {
                    "sent": d["radio_sent"],
                    "packets": self.ground_now["packets"],
                    "frames": self.ground_now["frames"],
                    "chunks": d["chunks"],
                }
            return
        if self.pass_start is None:
            return  # idle, and it was idle before - nothing to close out

        sent = d["radio_sent"] - self.pass_start["sent"]
        got = self.ground_now["packets"] - self.pass_start["packets"]
        frames = self.ground_now["frames"] - self.pass_start["frames"]
        pct = (got * 100 // sent) if sent else 0
        lines.append(
            f"{'LINK':<12} pass done  {self.pass_start['chunks']} chunks  "
            f"packets {got}/{sent} ({pct}%)  frames {frames}"
        )
        self.pass_start = None

    def _request_chunks(self, now: float, lines: list, tx: list) -> None:
        """The ground half of selective repeat: name the missing chunks while the vehicle sits
        idle in DOWNLINK, until there are none. Paced by this side's own clock, deliberately -
        anything paced by the vehicle's frames launches into the vehicle's transmit window."""
        if not self.transmit or now - self.dl_idle_at > DOWNLINK_FRESH_S:
            return  # mid-pass, out of DOWNLINK, or gone quiet - nobody is listening for this
        want = self.assembler.pending()
        if want is None or now - self.last_request < REQUEST_EVERY_S:
            return
        self.last_request = now
        image_id, _total, missing = want
        batch = missing[:CHUNK_REQUEST_MAX]
        tx.append(encode_chunk_request(image_id, batch))
        lines.append(
            f"{'REQUEST':<12} image {image_id}: resend {len(batch)} of "
            f"{len(missing)} missing chunks"
        )
