"""ANSI coloring for the console, matching the satellite's own status beads.

The point is that the box and the terminal teach each other. The WS2812 array on the comms plate
paints mode on bead 0, fault severity on bead 1, and link on bead 2 (`obc/Src/freertos/
control_task.cpp`), and the same reading of the same heartbeat picks the same colors here. Someone
who has learned that magenta means DOWNLINK on the rig has learned it for the console too.

Mirrored by hand rather than generated, which means it can drift - `test_colors` pins each mode
name against the bead's switch statement so a renamed mode fails a test instead of quietly losing
its color.
"""

import re

RESET = "\x1b[0m"

# xterm-256 where a basic color would be too dim or too close to a neighbour
_WHITE = "\x1b[1;37m"
_GREEN = "\x1b[1;32m"
_BLUE = "\x1b[1;34m"
_CYAN = "\x1b[1;36m"
_MAGENTA = "\x1b[1;35m"
_RED = "\x1b[1;31m"
_AMBER = "\x1b[33m"
_ORANGE = "\x1b[38;5;208m"
_YELLOW = "\x1b[1;33m"
_DIM = "\x1b[2m"

# bead 0 - mode. these are the ws2812 colors, one for one
MODE_COLORS = {
    "BOOT": _WHITE,
    "STANDBY": _GREEN,
    "DETUMBLE": _BLUE,
    "POINTING": _CYAN,
    "DOWNLINK": _MAGENTA,
    "SAFE": _RED,
}

# bead 1 - the fault ladder. a latched fault that is deliberately not acted on is blue rather than
# any shade of alarm, which is the whole distinction the bench build exists inside
SEVERITY_COLORS = {
    "Critical": _RED,
    "Degraded": _ORANGE,
    "Warning": _YELLOW,
    "Inhibited": _BLUE,
}

# bead 2 - the uplink
LINK_COLORS = {
    "up": _GREEN,
    "lost": _RED,
    "searching": _AMBER,
}


def paint(text: str, color: str, enabled: bool = True) -> str:
    """Wrap text in a color, or return it untouched when color is off."""
    return f"{color}{text}{RESET}" if enabled and color else text


# the kind's own color, kept so a heartbeat still reads as a heartbeat at a glance. only the label
# carries it - the fields after it are painted for what they say
HEARTBEAT_LABEL = _CYAN


def colorize_heartbeat(line: str, enabled: bool = True) -> str:
    """Color the mode and the faults inside an already-formatted HEARTBEAT line.

    Done by substitution on the finished line rather than inside `format_frame`, because that
    function feeds the SIL and HIL reports too - and escape codes belong on a terminal, not in a
    checked-in markdown artifact.

    With color on, the separate `inhibited={...}` set is dropped: every fault in `faults={...}` is
    already painted blue or red for exactly that distinction, and printing the same names twice on
    a line this long buys nothing. Without color the list stays, because then it is the only thing
    carrying the difference.
    """
    if not enabled:
        return line

    def mode_sub(m: re.Match) -> str:
        name = m.group(1)
        return "mode=" + paint(name, MODE_COLORS.get(name, ""))

    line = re.sub(r"mode=(\w+)", mode_sub, line)

    # read the inhibited set before anything is painted: faults= lists every latched fault,
    # inhibited ones included, and which color each name gets depends on being in this set
    inhibited_names = set()
    m = re.search(r"inhibited=\{([^}]*)\}", line)
    if m and m.group(1):
        inhibited_names = {s.strip() for s in m.group(1).split(",")}

    def fault_set_sub_named(label: str, body: str) -> str:
        if not body:
            return f"{label}={{}}"
        painted = []
        for name in (s.strip() for s in body.split(",")):
            # a latched fault nobody is acting on is not an alarm, on the bead or here
            color = SEVERITY_COLORS["Inhibited" if name in inhibited_names else "Critical"]
            painted.append(paint(name, color))
        return f"{label}={{" + ", ".join(painted) + "}"

    line = re.sub(r"faults=\{([^}]*)\}", lambda m: fault_set_sub_named("faults", m.group(1)), line)

    # and now the redundant copy goes, along with the double space it leaves behind
    line = re.sub(r"\s*inhibited=\{[^}]*\}", "", line)

    return re.sub(r"^(\S+)", lambda m: paint(m.group(1), HEARTBEAT_LABEL), line)
