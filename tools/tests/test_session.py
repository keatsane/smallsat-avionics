"""Tests for the console's protocol brain (ground/session.py).

Every link-layer bug this project has had - lost retries, phantom acks, requests sent to a
vehicle not listening - lived in logic that used to be closures inside uart_monitor.py, where no
test could reach it. These pin that logic with injected time, the same trick the HIL link monitor
uses.
"""

import math
import struct

from ground import frames
from ground.payload import Assembler
from ground.session import RETRY_AFTER_S, RETRY_LIMIT, GroundSession


def _ack(seq: int, accepted: bool = True) -> bytes:
    return frames.encode(frames.MSG_COMMAND_ACK, struct.pack("<BH2B", 1, seq, int(accepted), 0))


def _downlink_status(chunks: int, sent: int, image_id: int = 1, chunk: int = 0) -> bytes:
    payload = struct.pack("<IHHHIH", 1000, image_id, chunk, chunks, sent, 0)
    return frames.encode(frames.MSG_DOWNLINK_STATUS, payload)


def _ground_status(packets: int, nrf_frames: int) -> bytes:
    payload = struct.pack("<IIIIBB", 1000, 0, nrf_frames, packets, 0, 0x03)
    return frames.encode(frames.MSG_GROUND_STATUS, payload)


def _chunk(image_id: int, chunk: int, chunks: int, data: bytes) -> bytes:
    body = data + b"\x00" * (56 - len(data))
    payload = struct.pack("<HHHBB", image_id, chunk, chunks, len(data), 0) + body
    return frames.encode(frames.MSG_PAYLOAD_DATA, payload)


def test_command_goes_out_once_and_is_echoed():
    s = GroundSession(Assembler())
    lines, tx = s.command(1, 2, "SET_MODE DETUMBLE", now=0.0)
    assert len(tx) == 1
    assert "SENT" in lines[0] and "SET_MODE DETUMBLE" in lines[0] and "seq=1" in lines[0]


def test_unacked_command_is_retried_then_given_up_on():
    s = GroundSession(Assembler())
    s.command(1, 2, "SET_MODE DETUMBLE", now=0.0)

    # not due yet - nothing happens
    lines, tx = s.tick(RETRY_AFTER_S / 2)
    assert (lines, tx) == ([], [])

    # each overdue tick resends once, until the limit. the clock steps clear of the due time
    # rather than landing on it, because equality at a float boundary is not a behaviour worth
    # pinning either way
    t = 0.0
    for n in range(2, RETRY_LIMIT + 1):
        t += RETRY_AFTER_S + 0.05
        lines, tx = s.tick(t)
        assert len(tx) == 1
        assert f"(retry {n})" in lines[0]

    # past the limit the command is abandoned, loudly, and never sent again
    t += RETRY_AFTER_S + 0.05
    lines, tx = s.tick(t)
    assert tx == []
    assert "no ack" in lines[0]
    assert s.tick(t + RETRY_AFTER_S + 0.05) == ([], [])


def test_an_ack_stops_the_retries():
    s = GroundSession(Assembler())
    s.command(1, 2, "SET_MODE DETUMBLE", now=0.0)
    s.feed(_ack(seq=1), now=0.1)
    assert s.tick(10 * RETRY_AFTER_S) == ([], [])


def test_keepalives_are_never_retried():
    # the next keepalive is two seconds away and is a better retry than a resend of a stale one
    s = GroundSession(Assembler())
    s.command(0, 0, "NOOP (keepalive)", now=0.0, retry=False)
    assert s.tick(10 * RETRY_AFTER_S) == ([], [])


def test_read_only_session_never_requests_chunks():
    s = GroundSession(Assembler(), transmit=False)
    s.feed(_chunk(1, 0, 3, b"\xff\xd8"), now=0.0)  # 1 of 3 - incomplete
    s.feed(_downlink_status(chunks=0, sent=100), now=5.0)
    lines, tx = s.tick(6.0)
    assert tx == []


def test_missing_chunks_are_requested_on_this_sides_clock():
    # requests come from tick(), never from the arrival of a vehicle frame - a request triggered
    # by the vehicle's own status frame is phase-locked to the vehicle's transmit schedule, and
    # on the bench eight consecutive requests launched straight into its deaf window
    s = GroundSession(Assembler())
    # two of three chunks arrive; the vehicle finishes its pass and reports idle
    s.feed(_chunk(1, 0, 3, b"\xff\xd8"), now=0.0)
    s.feed(_chunk(1, 2, 3, b"\xff\xd9"), now=0.1)

    lines, tx = s.feed(_downlink_status(chunks=0, sent=100), now=5.0)
    assert tx == []  # the frame itself must trigger nothing

    lines, tx = s.tick(5.4)
    assert len(tx) == 1
    assert any("REQUEST" in ln and "resend 1 of 1" in ln for ln in lines)

    # rate-limited on this side's clock
    assert s.tick(5.9) == ([], [])

    # and once the vehicle has been quiet too long, requests stop - nobody is listening
    lines, tx = s.tick(30.0)
    assert tx == []


def test_no_request_while_a_pass_is_still_running():
    s = GroundSession(Assembler())
    s.feed(_chunk(1, 0, 3, b"\xff\xd8"), now=0.0)
    # chunks != 0 means the first transmission is still going - let it finish first
    s.feed(_downlink_status(chunks=3, sent=50), now=5.0)
    lines, tx = s.tick(6.0)
    assert tx == []


def test_a_pass_is_closed_out_with_its_delivery_rate():
    s = GroundSession(Assembler())
    s.feed(_ground_status(packets=100, nrf_frames=30), now=0.0)
    s.feed(_downlink_status(chunks=10, sent=1000), now=0.1)  # pass begins
    s.feed(_ground_status(packets=124, nrf_frames=38), now=1.0)
    lines, tx = s.feed(_downlink_status(chunks=0, sent=1030), now=2.0)  # pass ends

    link = [ln for ln in lines if ln.startswith("LINK")]
    assert len(link) == 1
    assert "10 chunks" in link[0]
    assert "packets 24/30 (80%)" in link[0]
    assert "frames 8" in link[0]


def test_firmware_text_between_frames_is_salvaged():
    s = GroundSession(Assembler())
    lines, _ = s.feed(b"BOOT: reset=pin\n", now=0.0)
    assert any(ln.startswith("TEXT") and "BOOT: reset=pin" in ln for ln in lines)

    # and a half line is surfaced when the port goes quiet rather than sat on
    s.feed(b"half a line", now=0.1)
    assert any("half a line" in ln for ln in s.idle())


# a tiny but genuine jpeg shape for the mission test - markers at both ends, three chunks long
JPEG = b"\xff\xd8" + bytes(range(120)) + b"\xff\xd9"


def _split(data: bytes) -> list:
    return [data[i : i + 56] for i in range(0, len(data), 56)]


def _heartbeat(mode_idx: int, seq: int = 1, faults: int = 0) -> bytes:
    return frames.encode(
        frames.MSG_HEARTBEAT, struct.pack("<IBIIHH", 1000, mode_idx, faults, 0, seq, 15000)
    )


def _attitude(
    heading_deg: float = 0.0,
    peak_dps: float = 0.0,
    target_deg: float = 0.0,
    in_band: bool = False,
) -> bytes:
    mrad = int(math.radians(heading_deg) * 1000)
    target = int(math.radians(target_deg) * 1000)
    peak = int(math.radians(peak_dps) * 1000)
    flags = frames.ATTITUDE_FLAG_IN_BAND if in_band else 0
    return frames.encode(
        frames.MSG_ATTITUDE_STATUS,
        struct.pack("<IhhhhhB", 1000, mrad, target, 0, 0, peak, flags),
    )


def _drain_acks(s: GroundSession, now: float) -> None:
    """Ack every outstanding command so retries stay out of the transcript under test."""
    for seq in list(s.outstanding):
        s.feed(_ack(seq), now=now)


def test_survey_flies_a_leg_per_bearing_and_stops_on_a_miss(tmp_path):
    # the capstone macro: several shoot legs at different bearings, advanced only by files
    # actually landing on disk. a panorama with a hole is worth stopping for
    s = GroundSession(Assembler(tmp_path))
    s.feed(_heartbeat(frames.MODES.index("STANDBY")), now=0.0)

    lines, tx = s.survey_start(3, 60.0, None, now=0.0)
    assert any("3 frames across 60 deg" in ln for ln in lines)
    assert any("leg 1/3 at 0 deg" in ln for ln in lines)
    assert len(tx) == 1  # the first leg's SET_MODE POINTING

    # the survey does not advance while a leg is still flying
    assert s.survey is not None
    assert s.survey["index"] == 1

    # a leg that lands no image stops the whole run rather than pressing on
    s.mission = None
    lines, _ = s.tick(1.0)
    assert any("stopped" in ln and "no image" in ln for ln in lines)
    assert s.survey is None


def test_chunk_requests_are_not_commands():
    # the console's keepalive fires after a quiet uplink, and "quiet" has to mean no COMMAND -
    # not no traffic. a selective-repeat request is uplink bytes routed to the downlink task, so
    # it never reaches the command handler and never resets the vehicle's link timer. counting it
    # as activity safed the rig nine seconds into a repair phase, with the ground transmitting
    # once a second the whole time
    assert frames.encode_command(0, 0, 1)[2] == frames.MSG_COMMAND
    assert frames.encode_chunk_request(1, [2, 3])[2] != frames.MSG_COMMAND


def test_aiming_waits_for_the_vehicle_to_echo_the_new_target(tmp_path):
    # the bug this pins: attitude frames computed against the OLD target are still in flight when
    # SET_HEADING goes up. One lands, sets in_band true again, and the macro shoots before the
    # slew has started - two survey runs took duplicate frames exactly that way. The handshake is
    # the vehicle's own echo of the target it is holding
    s = GroundSession(Assembler(tmp_path))
    s.feed(_heartbeat(frames.MODES.index("POINTING")), now=0.0)

    s.mission_start(None, 20.0, now=0.0)
    assert s.mission["step"] == "aim"

    # a stale frame: in band, but still reporting the old bearing. must not shoot
    s.feed(_attitude(target_deg=0.0, in_band=True), now=0.5)
    s.tick(0.6)
    assert s.mission["step"] == "aim"

    # the echo arrives and it is in band - now it may shoot. one tick to accept the aim and one
    # to fire, the same step-per-service shape the rest of the machine has
    s.feed(_attitude(target_deg=19.7, in_band=True), now=1.0)
    s.tick(1.1)
    lines, _ = s.tick(1.2)
    assert any("CAPTURE_IMAGE" in ln for ln in lines)


def test_survey_waits_between_legs_to_unwind_the_wheel(tmp_path):
    # every slew spends wheel momentum one way and only STANDBY gives it back, so a survey that
    # starts the next leg immediately hands it a wheel that is already part full
    s = GroundSession(Assembler(tmp_path))
    s.feed(_heartbeat(frames.MODES.index("STANDBY")), now=0.0)
    s.survey_start(3, 60.0, None, now=0.0)
    _drain_acks(s, 0.1)  # else the retry machinery resends leg 1 and muddies the transcript

    # first leg lands an image, so the survey continues - but not straight away
    s.mission = None
    s.images_saved = 1
    s.last_saved = "captures/image_0001.jpg"
    lines, tx = s.tick(1.0)
    assert any("unwinding the wheel" in ln for ln in lines)
    assert tx == []  # nothing commanded while it dumps

    assert s.tick(2.0) == ([], [])  # still dumping

    lines, tx = s.tick(1.0 + 4.0 + 0.1)
    assert any("survey leg 2/3" in ln for ln in lines)
    assert len(tx) == 1


def test_survey_refuses_a_single_frame(tmp_path):
    s = GroundSession(Assembler(tmp_path))
    lines, tx = s.survey_start(1, 60.0, None, now=0.0)
    assert tx == []
    assert any("at least two frames" in ln for ln in lines)


def test_breakaway_sweep_steps_up_until_the_platform_moves(tmp_path):
    # the measurement the rig actually needs: the torque the bearing lets go at, taken with
    # nothing plugged into the vehicle, because a cable across the joint is a torsion spring
    s = GroundSession(Assembler(tmp_path))
    s.feed(_heartbeat(frames.MODES.index("STANDBY")), now=0.0)
    s.feed(_attitude(10.0), now=0.0)

    lines, tx = s.sweep_start(4.0, 1.0, now=0.0)
    assert any("breakaway sweep" in ln for ln in lines)
    assert len(tx) == 1  # the first pulse
    _drain_acks(s, 0.1)

    # the platform did not move, so the next step is harder rather than the sweep stopping. one
    # tick to judge the step and one to fire the next, the same shape as the mission sequencer
    s.feed(_attitude(10.0), now=3.0)
    lines, _ = s.tick(3.5)
    assert any("4 mN m - still stuck" in ln for ln in lines)

    lines, tx = s.tick(3.6)
    assert any("PULSE_WHEEL 5" in ln for ln in lines)
    _drain_acks(s, 3.7)

    # this one moves it, and that torque is the answer
    s.feed(_attitude(peak_dps=18.0), now=7.0)
    lines, _ = s.tick(7.5)
    assert any("breakaway at 5 mN m" in ln for ln in lines)
    assert s.sweep is None


def test_breakaway_sweep_refuses_outside_standby(tmp_path):
    # PULSE_WHEEL is STANDBY-only in the flight software, so a sweep anywhere else would just
    # collect rejections and call the result a measurement
    s = GroundSession(Assembler(tmp_path))
    s.feed(_heartbeat(frames.MODES.index("POINTING")), now=0.0)

    lines, tx = s.sweep_start(4.0, 1.0, now=0.0)
    assert tx == []
    assert any("needs STANDBY" in ln for ln in lines)


def test_recover_waits_for_the_fault_before_commanding_the_mode(tmp_path):
    # the ordering is the whole point: while COMMAND_LINK_LOSS is still latched the executive
    # safes the vehicle again, so a SET_MODE sent alongside the CLEAR_FAULT is refused or undone
    s = GroundSession(Assembler(tmp_path))
    link_loss = 1 << frames.FAULTS.index("COMMAND_LINK_LOSS")
    safe = frames.MODES.index("SAFE")

    s.feed(_heartbeat(safe, faults=link_loss), now=0.0)

    lines, tx = s.recover_start(now=0.0)
    assert any("recover" in ln for ln in lines)
    assert len(tx) == 1  # the CLEAR_FAULT alone
    _drain_acks(s, 0.1)

    # the fault is still in the heartbeat, so the macro sits still however long it is ticked
    s.feed(_heartbeat(safe, faults=link_loss), now=1.0)
    assert s.tick(1.1) == ([], [])
    assert s.tick(2.0) == ([], [])

    # it clears, and only then does the mode command go out
    s.feed(_heartbeat(safe, faults=0), now=3.0)
    lines, tx = s.tick(3.1)
    assert any("SET_MODE STANDBY" in ln for ln in lines)
    _drain_acks(s, 3.2)

    lines, _ = s.tick(4.0)
    assert lines == []  # and it is not done until STANDBY is confirmed either
    s.feed(_heartbeat(frames.MODES.index("STANDBY")), now=4.5)
    lines, _ = s.tick(4.6)
    assert any("recovered" in ln for ln in lines)


def test_recover_gives_up_if_the_fault_never_clears(tmp_path):
    # a macro that waits forever is a macro that hides a dead uplink
    s = GroundSession(Assembler(tmp_path))
    link_loss = 1 << frames.FAULTS.index("COMMAND_LINK_LOSS")

    s.feed(_heartbeat(frames.MODES.index("SAFE"), faults=link_loss), now=0.0)
    s.recover_start(now=0.0)
    _drain_acks(s, 0.1)

    lines, _ = s.tick(11.0)
    assert any("failed" in ln for ln in lines)
    assert s.recover is None


def test_shoot_runs_the_whole_sequence_on_evidence(tmp_path):
    # the macro advances on what the telemetry shows - mode changes and the saved file - never on
    # the optimism of having sent something
    s = GroundSession(Assembler(tmp_path))

    lines, tx = s.mission_start(None, None, now=0.0)
    assert any("MISSION" in ln and "800x600" in ln for ln in lines)  # the default size
    assert len(tx) == 1  # SET_MODE POINTING went out immediately
    _drain_acks(s, 0.1)

    # nothing more until the vehicle proves it is POINTING. the machine takes one tick to see
    # the mode and one to act - the reader loop ticks at ~10 Hz, so that costs 100 ms on a bench
    assert s.tick(1.0) == ([], [])
    s.feed(_heartbeat(3), now=1.5)
    s.tick(1.6)
    lines, tx = s.tick(1.7)
    assert any("CAPTURE_IMAGE 800x600" in ln for ln in lines)
    _drain_acks(s, 1.8)

    # the settle pause, then DOWNLINK
    assert s.tick(2.0) == ([], [])
    lines, tx = s.tick(4.5)
    assert any("SET_MODE DOWNLINK" in ln for ln in lines)
    _drain_acks(s, 4.6)
    s.feed(_heartbeat(4), now=5.0)
    s.tick(5.1)

    # the image lands, and the macro parks the vehicle and reports the file
    parts = _split(JPEG)
    for i, part in enumerate(parts):
        s.feed(_chunk(1, i, len(parts), part), now=6.0)
    lines, tx = s.tick(6.5)
    assert any("SET_MODE STANDBY" in ln for ln in lines)
    _drain_acks(s, 6.6)
    lines, _ = s.feed(_heartbeat(1), now=7.0)
    lines2, _ = s.tick(7.1)
    assert any("MISSION" in ln and "complete" in ln for ln in lines + lines2)
    assert s.mission is None


def test_shoot_fails_loudly_when_the_mode_never_confirms(tmp_path):
    s = GroundSession(Assembler(tmp_path))
    s.mission_start(None, None, now=0.0)
    _drain_acks(s, 0.1)

    # the vehicle never reaches POINTING - past the deadline the mission says so and stands down
    lines, tx = s.tick(30.0)
    assert any("MISSION" in ln and "failed" in ln for ln in lines)
    assert s.mission is None
