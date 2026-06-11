"""Tests for the HIL runner's pure pieces (hil_runner.py).

LinkMonitor first (the SSA-020 spec, written before the implementation), then the
scenario loading and grading (SSA-022). All time is injected - no test here waits
on a real clock; the serial pump is untested plumbing whose proof is the bench.
"""

import struct

import pytest

from ground.frames import MSG_COMMAND_ACK, MSG_HEARTBEAT
from hil_runner import LinkMonitor, grade, load_scenario


def _hb(seq, uptime_ms=0, mode=1, faults=0):
    """An 11-byte heartbeat payload with a chosen seq (matches heartbeat_t)."""
    return struct.pack("<IBIH", uptime_ms, mode, faults, seq)


def _events(result):
    """Just the event names, for shape-only assertions."""
    return [e["event"] for e in result]


def test_link_up_on_first_heartbeat():
    mon = LinkMonitor(timeout_s=5.0)
    events = mon.on_frame(1.0, MSG_HEARTBEAT, _hb(seq=7))
    assert _events(events) == ["link_up"]
    assert events[0]["t"] == 1.0


def test_steady_heartbeats_no_events():
    mon = LinkMonitor(timeout_s=5.0)
    mon.on_frame(1.0, MSG_HEARTBEAT, _hb(seq=7))
    assert mon.on_frame(2.0, MSG_HEARTBEAT, _hb(seq=8)) == []


def test_non_heartbeat_ignored():
    # an ack mid-stream returns no events AND must not feed the heartbeat timer:
    # the link still goes lost on heartbeat silence even though the ack was recent
    mon = LinkMonitor(timeout_s=5.0)
    mon.on_frame(1.0, MSG_HEARTBEAT, _hb(seq=7))
    ack = struct.pack("<BHBB", 1, 7, 1, 0)
    assert mon.on_frame(4.0, MSG_COMMAND_ACK, ack) == []
    assert _events(mon.on_tick(6.1)) == ["link_loss"]


def test_no_loss_at_exactly_timeout():
    # strict >, mirroring the satellite-side dead man (REQ-CMD-002 semantics)
    mon = LinkMonitor(timeout_s=5.0)
    mon.on_frame(1.0, MSG_HEARTBEAT, _hb(seq=7))
    assert mon.on_tick(6.0) == []


def test_loss_just_past_timeout():
    mon = LinkMonitor(timeout_s=5.0)
    mon.on_frame(1.0, MSG_HEARTBEAT, _hb(seq=7))
    events = mon.on_tick(6.01)
    assert _events(events) == ["link_loss"]
    assert events[0]["last_seen"] == 1.0


def test_loss_reported_once():
    # loss is an edge, not a level - silent ticks after the first report say nothing
    mon = LinkMonitor(timeout_s=5.0)
    mon.on_frame(1.0, MSG_HEARTBEAT, _hb(seq=7))
    mon.on_tick(6.1)
    assert mon.on_tick(7.0) == []
    assert mon.on_tick(60.0) == []


def test_no_loss_before_first_heartbeat():
    # a link that never came up cannot be "lost" - that finding is the runner's
    mon = LinkMonitor(timeout_s=5.0)
    assert mon.on_tick(100.0) == []


def test_recovery_carries_outage():
    mon = LinkMonitor(timeout_s=5.0)
    mon.on_frame(1.0, MSG_HEARTBEAT, _hb(seq=7))
    mon.on_tick(6.1)
    events = mon.on_frame(13.0, MSG_HEARTBEAT, _hb(seq=8))
    assert _events(events) == ["recovery"]
    assert events[0]["outage_s"] == 12.0


def test_seq_gap():
    mon = LinkMonitor(timeout_s=5.0)
    mon.on_frame(1.0, MSG_HEARTBEAT, _hb(seq=7))
    mon.on_frame(2.0, MSG_HEARTBEAT, _hb(seq=8))
    events = mon.on_frame(3.0, MSG_HEARTBEAT, _hb(seq=10))
    assert _events(events) == ["seq_gap"]
    assert events[0]["missed"] == 1


def test_stats():
    mon = LinkMonitor(timeout_s=5.0)
    mon.on_frame(1.0, MSG_HEARTBEAT, _hb(seq=1))
    mon.on_frame(2.0, MSG_HEARTBEAT, _hb(seq=2))
    mon.on_frame(3.5, MSG_HEARTBEAT, _hb(seq=3))
    s = mon.stats()
    assert s["count"] == 2
    assert s["min"] == 1.0
    assert s["max"] == 1.5
    assert s["mean"] == 1.25


# --- grading (SSA-022) ---


def _ev(name, t=0.0, **extra):
    return {"event": name, "t": t, **extra}


def _stats(count=10, mn=0.95, mean=1.02, mx=1.12, crc=0):
    return {"count": count, "min": mn, "mean": mean, "max": mx, "crc_rejects": crc}


def test_grade_link_up_pass():
    checks = grade({"link": "up"}, [_ev("link_up")], _stats())
    assert [c.passed for c in checks] == [True]


def test_grade_link_up_fails_on_loss():
    events = [_ev("link_up"), _ev("link_loss")]
    checks = grade({"link": "up"}, events, _stats())
    assert [c.passed for c in checks] == [False]


def test_grade_recovered_with_outage():
    events = [_ev("link_up"), _ev("link_loss"), _ev("recovery", outage_s=10.4)]
    checks = grade({"link": "recovered", "outage_s": {"min": 5.0}}, events, _stats())
    assert all(c.passed for c in checks)


def test_grade_outage_too_short_fails():
    events = [_ev("link_up"), _ev("link_loss"), _ev("recovery", outage_s=2.0)]
    checks = grade({"outage_s": {"min": 5.0}}, events, _stats())
    assert [c.passed for c in checks] == [False]


def test_grade_period_bounds():
    expect = {"period_s": {"min": 0.8, "max": 1.3}}
    assert all(c.passed for c in grade(expect, [], _stats()))
    slow = _stats(mx=1.5)  # one period over the bound
    assert not all(c.passed for c in grade(expect, [], slow))
    assert not all(
        c.passed for c in grade(expect, [], _stats(count=0, mn=None, mean=None, mx=None))
    )


def test_grade_seq_gaps_and_crc():
    events = [_ev("link_up"), _ev("seq_gap", missed=2)]
    checks = grade({"seq_gaps": 0, "crc_rejects": 0}, events, _stats())
    assert [c.passed for c in checks] == [False, True]


def test_load_scenario_rejects_unknown_expect_key(tmp_path):
    bad = tmp_path / "bad.yaml"
    bad.write_text("id: HIL-X\ntitle: t\nduration_s: 5\nexpect:\n  bogus: 1\n")
    with pytest.raises(SystemExit) as e:
        load_scenario(bad)
    assert e.value.code == 2  # harness error, not a scenario fail
