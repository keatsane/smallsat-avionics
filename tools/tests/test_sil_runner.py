"""Tests for the SIL scenario runner's pure functions (sil_runner.py).

The subprocess/report plumbing is exercised end to end by the scenarios themselves;
these cover the transforms: timeline compile, output parse, fault replay, grading.
"""

import struct

from ground.frames import MSG_COMMAND_ACK, encode
from sil_runner import Event, compile_timeline, grade, parse_output, replay_fault_set


def _ack_frame_line(cmd_id, seq, accepted, reason):
    payload = struct.pack("<BHBB", cmd_id, seq, accepted, reason)
    return "FRAME " + encode(MSG_COMMAND_ACK, payload).hex()


def test_compile_quiet_step():
    assert compile_timeline([{"t": 200}]) == "200\n"


def test_compile_fault_and_cmd_share_a_line():
    # a step may carry both - the elif bug this guards against dropped the fault
    step = {
        "t": 400,
        "fault": {"name": "UNDERVOLTAGE", "bad": 1},
        "cmd": {"name": "NOOP", "arg": 0, "seq": 3},
    }
    assert compile_timeline([step]) == "400 fault UNDERVOLTAGE 1 cmd NOOP 0 3\n"


def test_parse_output_events_and_acks():
    stdout = "\n".join(
        [
            "CYCLE t=100",
            _ack_frame_line(cmd_id=1, seq=1, accepted=1, reason=0),
            "EVENT mode t=100 from=BOOT to=STANDBY trigger=Command req=REQ-CMD-001",
            "END t=200 mode=STANDBY faults=0x00000000",
        ]
    )
    events, acks = parse_output(stdout)
    assert [e.tag for e in events] == ["CYCLE", "EVENT", "END"]
    assert events[1].kind == "mode"
    assert events[1].fields["to"] == "STANDBY"
    assert acks == [{"cmd_id": 1, "seq": 1, "accepted": True, "reason": "Ok"}]


def test_replay_fault_set_latch_then_clear():
    events = [
        Event("EVENT", "fault", {"t": "100", "fault": "UNDERVOLTAGE", "edge": "latch"}),
        Event("EVENT", "fault", {"t": "200", "fault": "ACCEL_GYRO_DROPOUT", "edge": "latch"}),
        Event("EVENT", "fault", {"t": "300", "fault": "UNDERVOLTAGE", "edge": "clear"}),
    ]
    assert replay_fault_set(events) == {"ACCEL_GYRO_DROPOUT"}


def _safe_entry_events():
    return [
        Event("EVENT", "fault", {"t": "400", "fault": "UNDERVOLTAGE", "edge": "latch"}),
        Event(
            "EVENT",
            "mode",
            {
                "t": "400",
                "from": "STANDBY",
                "to": "SAFE",
                "trigger": "FaultEntry",
                "req": "REQ-FAULT-002",
            },
        ),
        Event("END", "", {"t": "500", "mode": "SAFE", "faults": "0x00000010"}),
    ]


def test_grade_passes_on_matching_run():
    expect = {
        "mode_log": [{"to": "SAFE", "trigger": "FaultEntry", "req": "REQ-FAULT-002"}],
        "final": {"mode": "SAFE", "faults_set": ["UNDERVOLTAGE"]},
    }
    checks = grade(expect, _safe_entry_events(), acks=[])
    assert all(c.passed for c in checks)


def test_grade_fails_on_wrong_final_mode():
    expect = {"final": {"mode": "STANDBY"}}
    checks = grade(expect, _safe_entry_events(), acks=[])
    assert not all(c.passed for c in checks)


def test_grade_fails_safe_for_the_wrong_reason():
    # SAFE was reached, but not via the expected trigger - this must NOT pass
    expect = {"mode_log": [{"to": "SAFE", "trigger": "Command", "req": "REQ-FAULT-002"}]}
    checks = grade(expect, _safe_entry_events(), acks=[])
    assert not checks[0].passed


def _ack_checks(expect_acks, acks):
    checks = grade({"acks": expect_acks, "final": {}}, _safe_entry_events(), acks)
    return [c for c in checks if c.name.startswith("ack")]


def test_grade_matches_ack_by_seq():
    acks = [{"cmd_id": 3, "seq": 2, "accepted": False, "reason": "IllegalInMode"}]
    good = _ack_checks([{"seq": 2, "accepted": False, "reason": "IllegalInMode"}], acks)
    assert good[0].passed


def test_grade_fails_ack_with_wrong_reason():
    acks = [{"cmd_id": 3, "seq": 2, "accepted": False, "reason": "IllegalInMode"}]
    bad = _ack_checks([{"seq": 2, "accepted": False, "reason": "BadArg"}], acks)
    assert not bad[0].passed
