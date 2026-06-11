"""Tests for the shared runner core's scenario resolver (ground/runner.py)."""

import pytest

from ground.runner import resolve_scenarios


@pytest.fixture
def scenario_dir(tmp_path):
    for name in (
        "sil_001_undervoltage.yaml",
        "sil_002_illegal_command.yaml",
        "sil_010_ten.yaml",
    ):
        (tmp_path / name).write_text("x")
    return tmp_path


def test_all_returns_every_yaml_sorted(scenario_dir):
    files = resolve_scenarios(["all"], scenario_dir, "sil")
    assert [f.name for f in files] == [
        "sil_001_undervoltage.yaml",
        "sil_002_illegal_command.yaml",
        "sil_010_ten.yaml",
    ]


def test_number_is_zero_padded_not_substring(scenario_dir):
    # "1" must resolve to 001 only - a substring match would also catch 010
    files = resolve_scenarios(["1"], scenario_dir, "sil")
    assert [f.name for f in files] == ["sil_001_undervoltage.yaml"]


def test_name_fragment(scenario_dir):
    files = resolve_scenarios(["illegal"], scenario_dir, "sil")
    assert [f.name for f in files] == ["sil_002_illegal_command.yaml"]


def test_explicit_path_passes_through(scenario_dir):
    path = scenario_dir / "sil_001_undervoltage.yaml"
    assert resolve_scenarios([str(path)], scenario_dir, "sil") == [path]


def test_duplicates_collapse(scenario_dir):
    # the same scenario referenced twice runs once
    files = resolve_scenarios(["2", "illegal"], scenario_dir, "sil")
    assert [f.name for f in files] == ["sil_002_illegal_command.yaml"]


def test_no_match_is_a_harness_error(scenario_dir):
    with pytest.raises(SystemExit) as e:
        resolve_scenarios(["9"], scenario_dir, "sil")
    assert e.value.code == 2
