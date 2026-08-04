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
    ATTITUDE_FLAG_IN_BAND,
    CHUNK_REQUEST_MAX,
    COMMANDS,
    FAULTS,
    MODES,
    MSG_ATTITUDE_STATUS,
    MSG_HEARTBEAT,
    RESOLUTIONS,
    MSG_COMMAND_ACK,
    MSG_DOWNLINK_STATUS,
    MSG_GROUND_STATUS,
    MSG_PAYLOAD_DATA,
    FrameDecoder,
    decode_attitude_status,
    decode_command_ack,
    decode_heartbeat,
    decode_downlink_status,
    decode_ground_status,
    decode_payload_data,
    encode_chunk_request,
    encode_command,
    format_frame,
    heading_arg,
    torque_arg,
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

# the breakaway sweep. a pulse lasts half a second on the vehicle, so the settle window has to
# cover that plus however long the platform takes to stop - it decelerates at a few rad/s^2, and
# the attitude frame the ground reads arrives once a second over the radio
SWEEP_SETTLE_S = 3.0

# the peak body rate a pulse must produce to count as having broken the bearing loose. the gyro
# rests within a few tenths of a degree per second, and the bench's smallest real jolts came in
# above ten, so this sits clear of the noise without needing the platform to travel anywhere
SWEEP_MOVED_DPS = 3.0

# where to give up. the controller's own ceiling is 50 mN m, so a bearing that has not let go by
# here is not one POINTING can steer against at all
SWEEP_MAX_MNM = 50.0

# how long a survey sits in STANDBY between legs so the vehicle can unwind its wheel. the dump
# runs at 3 mN m against a wheel holding at most 3.9e-3 kg m^2/s, so a full one empties in a bit
# over a second; four is that with margin, and it is cheap next to a downlink pass
SURVEY_DUMP_S = 4.0

# how close the vehicle's echoed target must be to the commanded one to count as "it has this
# command". SET_HEADING is a binary angle, one byte over a full turn, so the echo lands on a
# 1.4-degree grid and never matches exactly
AIM_TARGET_TOL_DEG = 1.0


class GroundSession:
    """Byte stream + clock in, display lines + uplink bytes out."""

    def __init__(
        self,
        assembler: Assembler,
        retry: bool = True,
        transmit: bool = True,
        on_attitude=None,
    ):
        self.assembler = assembler
        self.retry = retry  # False = every command is fire-and-forget (--no-retry)
        self.transmit = transmit  # False = never produce uplink bytes (--read-only)
        # called with each decoded attitude frame, for whoever wants the stream rather than the
        # display line - the console uses it to record a run to csv. optional on purpose: the
        # session's job is protocol, and where the data ends up is not its business
        self.on_attitude = on_attitude

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

        # vehicle state gleaned from the passing stream, for the mission sequencer
        self.mode = ""  # last heartbeat's mode name
        self.faults = 0  # last heartbeat's latched-fault mask, for the recover macro
        self.in_band = False  # last attitude frame's pointing flag
        self.heading = 0.0  # last attitude frame's heading, degrees
        self.target = 0.0  # and the bearing it says it is holding - the aim step's handshake
        self.pulse_peak = 0.0  # peak rate since the vehicle's last pulse - the sweep's ruler
        self.images_saved = 0
        self.last_saved: str = ""

        # the mission sequencer - the shoot macro's state. one mission at a time, advanced by
        # tick() against what the telemetry stream actually shows happening, because "the mode
        # changed" is evidence and "the command was sent" is only hope
        self.mission: dict | None = None
        self.recover: dict | None = None
        self.sweep: dict | None = None
        self.survey: dict | None = None

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
            self._sweep_service(now, lines, tx)
            self._recover_service(now, lines, tx)
            self._mission_service(now, lines, tx)
            self._survey_service(now, lines, tx)
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

        if msg_id == MSG_HEARTBEAT:
            hb = decode_heartbeat(payload)
            self.mode = hb["mode"]
            self.faults = hb["faults"]
        elif msg_id == MSG_ATTITUDE_STATUS:
            att = decode_attitude_status(payload)
            self.in_band = bool(att["flags"] & ATTITUDE_FLAG_IN_BAND)
            self.heading = att["heading_deg"]
            self.target = att["target_deg"]
            self.pulse_peak = att["pulse_peak_dps"]
            if self.on_attitude is not None:
                self.on_attitude(att)
        elif msg_id == MSG_COMMAND_ACK:
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
        self.images_saved += 1
        self.last_saved = str(path)
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

    def sweep_start(self, start_mnm: float, step_mnm: float, now: float):
        """Find the bearing's breakaway torque by pulsing the wheel harder until it moves.

        The vehicle runs the pulses, so this works with nothing plugged into it - which is the
        whole point. A cable across the rotating joint is a torsion spring, and a bench sweep with
        one attached measured the cable rather than the bearing.

        Each step reads the heading, fires one bounded pulse, waits for the platform to settle,
        and reads the heading again. The first step that moves it past the noise floor is the
        answer, in the units the flight software commands.
        """
        lines: list[str] = []
        tx: list[bytes] = []
        with self.lock:
            if self.sweep is not None:
                return [f"{'MISSION':<12} already sweeping"], []
            if self.mode != "STANDBY":
                return [f"{'MISSION':<12} sweep needs STANDBY - the pulse is refused elsewhere"], []
            self.sweep = {"torque": start_mnm, "step": step_mnm, "phase": "fire", "due": now}
            lines.append(f"{'MISSION':<12} breakaway sweep from {start_mnm:g} mN m, +{step_mnm:g}")
            self._sweep_service(now, lines, tx)
        return lines, tx

    def _sweep_service(self, now: float, lines: list, tx: list) -> None:
        """One pulse per cycle: fire, settle, compare the heading, decide."""
        s = self.sweep
        if s is None or now < s["due"]:
            return

        if s["phase"] == "fire":
            self.pulse_peak = 0.0  # the vehicle clears its own on the command; match it here
            self._mission_send(
                "PULSE_WHEEL",
                torque_arg(s["torque"]),
                f"PULSE_WHEEL {s['torque']:g}",
                now,
                tx,
                lines,
            )
            # the vehicle holds the pulse for half a second; the rest is letting the platform stop
            s["phase"], s["due"] = "settle", now + SWEEP_SETTLE_S
            return

        # the vehicle's own peak, not a difference between two samples of a slow stream. the
        # platform's answer to a pulse is a transient that starts and finishes between frames
        peak = self.pulse_peak
        if peak >= SWEEP_MOVED_DPS:
            lines.append(
                f"{'MISSION':<12} breakaway at {s['torque']:g} mN m - the platform turned at "
                f"{peak:.1f} deg/s"
            )
            self.sweep = None
            return

        lines.append(f"{'MISSION':<12} {s['torque']:g} mN m - still stuck (peak {peak:.1f} deg/s)")
        s["torque"] += s["step"]
        if s["torque"] > SWEEP_MAX_MNM:
            lines.append(f"{'MISSION':<12} sweep reached {SWEEP_MAX_MNM:g} mN m without moving it")
            self.sweep = None
            return
        s["phase"], s["due"] = "fire", now

    def recover_start(self, now: float):
        """Clear the command-link fault and put the vehicle back in STANDBY.

        Two commands rather than one, and ordered: while the fault is still latched the executive
        safes the vehicle again, so a SET_MODE sent alongside the CLEAR_FAULT would be refused or
        immediately undone. The second step waits for the heartbeat to show the fault actually
        gone - the same advance-on-evidence rule the shoot macro follows.
        """
        lines: list[str] = []
        tx: list[bytes] = []
        with self.lock:
            if self.recover is not None:
                return [f"{'MISSION':<12} already recovering"], []
            self.recover = {"step": "clear", "due": now}
            lines.append(f"{'MISSION':<12} recover - clear the link fault, back to STANDBY")
            self._recover_service(now, lines, tx)
        return lines, tx

    def _recover_service(self, now: float, lines: list, tx: list) -> None:
        """Advance the recover macro by what the heartbeat shows, not by what was sent."""
        r = self.recover
        if r is None:
            return

        if r["step"] == "clear":
            self._mission_send(
                "CLEAR_FAULT",
                FAULTS.index("COMMAND_LINK_LOSS"),
                "CLEAR_FAULT COMMAND_LINK_LOSS",
                now,
                tx,
                lines,
            )
            r["step"], r["due"] = "clear_wait", now + 10.0
        elif r["step"] == "clear_wait":
            if not (self.faults & (1 << FAULTS.index("COMMAND_LINK_LOSS"))):
                self._mission_send(
                    "SET_MODE", MODES.index("STANDBY"), "SET_MODE STANDBY", now, tx, lines
                )
                r["step"], r["due"] = "standby_wait", now + 10.0
            elif now >= r["due"]:
                lines.append(f"{'MISSION':<12} recover failed - the fault never cleared")
                self.recover = None
        elif r["step"] == "standby_wait":
            if self.mode == "STANDBY":
                lines.append(f"{'MISSION':<12} recovered - STANDBY, link fault cleared")
                self.recover = None
            elif now >= r["due"]:
                lines.append(f"{'MISSION':<12} recover failed - STANDBY never confirmed")
                self.recover = None

    def survey_start(self, count: int, span_deg: float, resolution, now: float):
        """The capstone: point, shoot and downlink at several bearings, then stitch the set.

        Deliberately a ground macro rather than a SURVEY mode, which is what the roadmap first
        sketched. A mode is a posture - something the vehicle *is* - and a survey is a sequence
        of postures it already has. Adding one would have meant a mode that behaves exactly like
        POINTING except for who is counting, which is the thing `roadmap.md`'s own design rule
        warns against. The shoot macro settled this pattern first and this is its plural.
        """
        lines: list[str] = []
        tx: list[bytes] = []
        with self.lock:
            if self.survey is not None or self.mission is not None:
                return [f"{'MISSION':<12} already flying - one at a time"], []
            if count < 2:
                return [f"{'MISSION':<12} a survey needs at least two frames"], []
            res = resolution or "800x600"
            if res not in RESOLUTIONS:
                return [f"{'MISSION':<12} unknown size {res!r}"], []

            # evenly spaced across the span, starting at the heading POINTING is entered on - the
            # same frame SET_HEADING already works in, so what is commanded is what is meant
            step = span_deg / float(count - 1)
            self.survey = {
                "bearings": [i * step for i in range(count)],
                "index": 0,
                "res": res,
                "files": [],
                "leg_open": False,
            }
            lines.append(
                f"{'MISSION':<12} survey - {count} frames across {span_deg:g} deg at {res}"
            )
            self._survey_service(now, lines, tx)
        return lines, tx

    def _survey_service(self, now: float, lines: list, tx: list) -> None:
        """Fly one leg at a time, advancing only when the last one has landed a file."""
        s = self.survey
        if s is None or self.mission is not None:
            return  # a leg is still in the air

        if s["leg_open"]:
            # the leg finished - it either produced a file or it did not, and a panorama with a
            # hole in it is worth stopping for rather than papering over
            s["leg_open"] = False
            if self.images_saved > s["saved_at_leg"]:
                s["files"].append(self.last_saved)
                lines.append(
                    f"{'MISSION':<12} survey {s['index']}/{len(s['bearings'])} -> {self.last_saved}"
                )
            else:
                lines.append(f"{'MISSION':<12} survey stopped - leg {s['index']} landed no image")
                self.survey = None
                return
            # and then wait, which is the part a survey cannot skip. every slew spends wheel
            # momentum in one direction and the vehicle only unwinds it in STANDBY, so starting
            # the next leg the instant the last one parked hands it a wheel that is already
            # half full. On the bench the third leg had nothing left and crawled 7 degrees in 25
            # seconds. This is momentum management at the mission level, and it is why real
            # survey plans have dwell in them
            s["dump_until"] = now + SURVEY_DUMP_S
            lines.append(f"{'MISSION':<12} unwinding the wheel for {SURVEY_DUMP_S:g} s")

        if now < s.get("dump_until", 0.0):
            return

        if s["index"] >= len(s["bearings"]):
            names = " ".join(s["files"])
            lines.append(f"{'MISSION':<12} survey complete - {len(s['files'])} frames")
            lines.append(f"{'MISSION':<12} stitch with: python tools/stitch.py {names}")
            self.survey = None
            return

        bearing = s["bearings"][s["index"]]
        s["index"] += 1
        s["saved_at_leg"] = self.images_saved
        s["leg_open"] = True
        lines.append(
            f"{'MISSION':<12} survey leg {s['index']}/{len(s['bearings'])} at {bearing:g} deg"
        )
        self.mission = {
            "step": "point",
            "due": now,
            "res": s["res"],
            "bearing": bearing,
            "saved_before": self.images_saved,
        }
        self._mission_service(now, lines, tx)

    def mission_start(self, resolution, bearing_deg, now: float):
        """Begin the whole imaging sequence as one command: point, (aim), shoot, downlink, park.

        A ground-side macro rather than flight autonomy on purpose: the sequencing lives where it
        is visible and abortable, and every step is advanced by what the telemetry shows actually
        happened rather than by optimism about what was sent.
        """
        lines: list[str] = []
        tx: list[bytes] = []
        with self.lock:
            if self.mission is not None:
                return [f"{'MISSION':<12} already flying - one at a time"], []
            res = resolution or "800x600"
            if res not in RESOLUTIONS:
                choices = ", ".join(RESOLUTIONS)
                return [f"{'MISSION':<12} unknown size {res!r} - one of {choices}"], []
            self.mission = {
                "step": "point",
                "due": now,  # first step fires immediately
                "res": res,
                "bearing": bearing_deg,
                "saved_before": self.images_saved,
            }
            tail = f" at {bearing_deg:g} deg" if bearing_deg is not None else ""
            lines.append(f"{'MISSION':<12} shoot {res}{tail}")
            self._mission_service(now, lines, tx)
        return lines, tx

    def _mission_send(self, cmd_name: str, arg: int, label: str, now: float, tx: list, lines: list):
        """One mission step's command, through the normal retry machinery."""
        self.seq = (self.seq + 1) & 0xFFFF
        frame = encode_command(COMMANDS.index(cmd_name), arg, self.seq)
        if self.retry:
            self.outstanding[self.seq] = {
                "frame": frame,
                "label": label,
                "tries": 1,
                "due": now + RETRY_AFTER_S,
            }
        tx.append(frame)
        lines.append(f"{'SENT':<12} {label}  seq={self.seq}")

    def _mission_fail(self, why: str, lines: list) -> None:
        lines.append(f"{'MISSION':<12} failed - {why}")
        self.mission = None

    def _aim_or_shoot(self, m: dict, now: float, lines: list, tx: list) -> None:
        if m["bearing"] is not None:
            self._mission_send(
                "SET_HEADING",
                heading_arg(m["bearing"]),
                f"SET_HEADING {m['bearing']:g}",
                now,
                tx,
                lines,
            )
            # do not trust the in-band flag until the vehicle proves it has this command. clearing
            # the flag locally is not enough: frames computed against the OLD target are still in
            # flight and one of them lands a moment later, sets it true again, and the macro shoots
            # before the slew starts. Two survey runs took duplicate frames that way.
            #
            # the handshake that does work is the vehicle's own echo - attitude telemetry reports
            # the target it is holding, so the aim step waits for that to match what was asked for
            # and only then starts believing the band flag
            m["aim_target"] = heading_arg(m["bearing"]) * 360.0 / 256.0
            self.in_band = False
            m["step"], m["due"] = "aim", now + 25.0
        else:
            m["step"], m["due"] = "shoot", now

    def _mission_service(self, now: float, lines: list, tx: list) -> None:
        """Advance the mission by evidence: mode changes, the in-band flag, the saved file."""
        m = self.mission
        if m is None:
            return
        step = m["step"]

        if step == "point":
            if self.mode == "POINTING":
                self._aim_or_shoot(m, now, lines, tx)
            else:
                self._mission_send("SET_MODE", 3, "SET_MODE POINTING", now, tx, lines)
                m["step"], m["due"] = "point_wait", now + 10.0
        elif step == "point_wait":
            if self.mode == "POINTING":
                self._aim_or_shoot(m, now, lines, tx)
            elif now >= m["due"]:
                self._mission_fail("POINTING never confirmed", lines)
        elif step == "aim":
            # wait for the controller to report in band. without a wheel this only succeeds if
            # the platform already points there, so the timeout proceeds rather than fails - a
            # photograph of the wrong bearing is still a photograph, and the log says which
            acked = abs(self.target - m.get("aim_target", self.target)) < AIM_TARGET_TOL_DEG
            if (acked and self.in_band) or now >= m["due"]:
                if not (acked and self.in_band):
                    lines.append(f"{'MISSION':<12} aim timed out - shooting where it points")
                m["step"] = "shoot"
        elif step == "shoot":
            self._mission_send(
                "CAPTURE_IMAGE",
                RESOLUTIONS.index(m["res"]),
                f"CAPTURE_IMAGE {m['res']}",
                now,
                tx,
                lines,
            )
            # the capture is fire-and-forget on the vehicle; give the fifo a moment to fill
            m["step"], m["due"] = "settle", now + 2.5
        elif step == "settle":
            if now >= m["due"]:
                self._mission_send("SET_MODE", 4, "SET_MODE DOWNLINK", now, tx, lines)
                m["step"], m["due"] = "downlink_wait", now + 10.0
        elif step == "downlink_wait":
            if self.mode == "DOWNLINK":
                m["step"], m["due"] = "receive", now + 120.0
            elif now >= m["due"]:
                self._mission_fail("DOWNLINK never confirmed", lines)
        elif step == "receive":
            if self.images_saved > m["saved_before"]:
                self._mission_send("SET_MODE", 1, "SET_MODE STANDBY", now, tx, lines)
                m["step"], m["due"] = "park", now + 10.0
            elif now >= m["due"]:
                self._mission_fail("image never completed", lines)
        elif step == "park":
            if self.mode == "STANDBY":
                lines.append(f"{'MISSION':<12} complete -> {self.last_saved}")
                self.mission = None
            elif now >= m["due"]:
                # the picture is on disk; a vehicle that would not park is an anticlimax, not
                # a failure
                lines.append(
                    f"{'MISSION':<12} complete (vehicle left in DOWNLINK) -> {self.last_saved}"
                )
                self.mission = None

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
