"""The console's colors against the satellite's status beads.

These are hand-mirrored from `obc/Src/freertos/control_task.cpp`, so the drift test below is the
only thing keeping them honest: a mode renamed on the vehicle has to fail here rather than quietly
losing its color on the console.
"""

import re

from pathlib import Path

from ground.colors import HEARTBEAT_LABEL, MODE_COLORS, colorize_heartbeat, paint
from ground.frames import MODES

REPO_ROOT = Path(__file__).resolve().parents[2]


def test_every_mode_has_a_color():
    assert set(MODE_COLORS) == set(MODES)


def test_mode_colors_match_the_status_bead():
    # bead 0's switch names its color in a trailing comment on each arm - the same words this
    # module uses. a mode whose bead color changes on the rig should change here too
    source = (REPO_ROOT / "obc" / "Src" / "freertos" / "control_task.cpp").read_text()
    bead = source[source.index("switch (e.modes().mode())") :]
    bead = bead[: bead.index("set_fault_bead")]

    found = dict(
        re.findall(r"case fsw::Mode::(\w+):\s*\n\s*ws2812_set\(0U[^;]*;\s*//\s*(\w+)", bead)
    )
    assert set(found) == set(MODES), "the mode bead no longer covers every mode"

    names = {
        "white": "BOOT",
        "green": "STANDBY",
        "blue": "DETUMBLE",
        "cyan": "POINTING",
        "magenta": "DOWNLINK",
        "red": "SAFE",
    }
    for mode, color_word in found.items():
        assert names[color_word] == mode, f"{mode} is {color_word} on the bead but not here"


def test_colorize_paints_the_mode():
    line = "HEARTBEAT    t=1 ms  mode=DOWNLINK  faults={}  seq=1"
    out = colorize_heartbeat(line, enabled=True)
    assert paint("DOWNLINK", MODE_COLORS["DOWNLINK"]) in out


def test_the_kind_keeps_its_own_color():
    line = "HEARTBEAT    t=1 ms  mode=SAFE  faults={}  seq=1"
    out = colorize_heartbeat(line, enabled=True)
    assert out.startswith(HEARTBEAT_LABEL + "HEARTBEAT")


def test_the_redundant_inhibited_list_is_dropped_when_colored():
    # the color already says which faults are inhibited, so the second copy of the same names is
    # noise on an already-long line. without color it has to stay - see the no-op test below
    line = (
        "HEARTBEAT    t=1 ms  mode=STANDBY  faults={COMMAND_LINK_LOSS}  "
        "inhibited={COMMAND_LINK_LOSS}  seq=1"
    )
    out = colorize_heartbeat(line, enabled=True)
    assert "inhibited=" not in out
    assert out.count("COMMAND_LINK_LOSS") == 1
    assert "seq=1" in out


def test_inhibited_and_acting_faults_are_painted_differently():
    line = (
        "HEARTBEAT    t=1 ms  mode=STANDBY  faults={COMMAND_LINK_LOSS, UNDERVOLTAGE}  "
        "inhibited={COMMAND_LINK_LOSS}  seq=1"
    )
    out = colorize_heartbeat(line, enabled=True)

    # the whole point of the distinction: a latched fault nobody is acting on must not look like
    # an alarm, which is the same call the fault bead makes when it goes blue
    inhibited_color = out.split("COMMAND_LINK_LOSS")[0][-8:]
    acting_color = out.split("UNDERVOLTAGE")[0][-8:]
    assert inhibited_color != acting_color


def test_colorize_is_a_no_op_when_disabled():
    # piped output and the checked-in reports go through here too, and both need the inhibited
    # list intact - with no color it is the only thing carrying the distinction
    line = "HEARTBEAT    t=1 ms  mode=SAFE  faults={UNDERVOLTAGE}  inhibited={UNDERVOLTAGE}  seq=1"
    assert "inhibited=" in colorize_heartbeat(line, enabled=False)
    assert colorize_heartbeat(line, enabled=False) == line
    assert "\x1b" not in colorize_heartbeat(line, enabled=False)


def test_an_empty_fault_set_stays_empty():
    line = "HEARTBEAT    t=1 ms  mode=STANDBY  faults={}  seq=1"
    assert "faults={}" in colorize_heartbeat(line, enabled=True)
