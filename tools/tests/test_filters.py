"""Tests for the console's show/hide state (ground/filters.py).

The directives look trivial and are not: `show` has to widen whichever set is in force, and doing
the obvious thing - removing the kind from `only` - hides the thing that was just asked for.
"""

from ground.filters import ALWAYS_SHOWN, KINDS, PER_CYCLE_KINDS, Filters


def test_the_default_hides_only_the_per_cycle_stream():
    f = Filters.quiet()
    assert f.visible("HEARTBEAT")
    assert f.visible("TASKS")
    for kind in PER_CYCLE_KINDS:
        assert not f.visible(kind)


def test_all_shows_everything():
    f = Filters.quiet()
    f.apply("all")
    assert all(f.visible(kind) for kind in KINDS)


def test_quiet_returns_to_the_default_from_anywhere():
    f = Filters(only={"POWER"})
    f.apply("quiet")
    assert f.visible("HEARTBEAT")
    assert not f.visible("IMU")


def test_only_excludes_everything_unnamed():
    f = Filters.quiet()
    f.apply("only", {"HEARTBEAT", "POWER"})
    assert f.visible("HEARTBEAT")
    assert f.visible("POWER")
    assert not f.visible("TASKS")
    assert not f.visible("TEMP")


def test_hide_removes_a_kind_that_was_showing():
    f = Filters.quiet()
    assert f.visible("TASKS")
    f.apply("hide", {"TASKS"})
    assert not f.visible("TASKS")


def test_show_reveals_a_hidden_kind():
    f = Filters.quiet()
    f.apply("show", {"IMU"})
    assert f.visible("IMU")
    assert not f.visible("POWER")  # the rest of the per-cycle stream stays hidden


def test_show_widens_an_only_set_rather_than_narrowing_it():
    # the bug worth a test: dropping the kind from `only` would hide what was just asked for
    f = Filters(only={"HEARTBEAT"})
    f.apply("show", {"POWER"})
    assert f.visible("HEARTBEAT")
    assert f.visible("POWER")
    assert not f.visible("IMU")


def test_hide_inside_an_only_set_removes_it_from_the_whitelist():
    f = Filters(only={"HEARTBEAT", "POWER"})
    f.apply("hide", {"POWER"})
    assert f.visible("HEARTBEAT")
    assert not f.visible("POWER")


def test_local_echo_is_never_filtered():
    # hiding what was just typed is how you sit there wondering why nothing happened
    f = Filters(only={"HEARTBEAT"})
    for kind in ALWAYS_SHOWN:
        assert f.visible(kind)
    f.apply("hide", set(ALWAYS_SHOWN))
    for kind in ALWAYS_SHOWN:
        assert f.visible(kind)


def test_an_unknown_verb_changes_nothing_and_reports_it():
    f = Filters.quiet()
    assert f.apply("bogus", {"IMU"}) is False
    assert not f.visible("IMU")  # untouched
    assert f.visible("HEARTBEAT")


def test_a_kindless_verb_that_needs_kinds_is_refused():
    f = Filters.quiet()
    assert f.apply("only", set()) is False
    assert f.visible("HEARTBEAT")  # still the default, not an empty whitelist hiding everything


def test_filters_verb_reports_without_changing():
    f = Filters.quiet()
    assert f.apply("filters") is True
    assert not f.visible("IMU")


def test_describe_reads_naturally_in_each_state():
    assert Filters().describe() == "showing everything"
    assert Filters(hide={"IMU"}).describe() == "hiding IMU"
    assert Filters(only={"POWER", "HEARTBEAT"}).describe() == "showing only HEARTBEAT, POWER"
