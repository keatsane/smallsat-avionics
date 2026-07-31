"""Which telemetry kinds the console shows.

Kept apart from the console itself because the interesting behaviour is not the typing, it is what
`show` means when an `only` set is already in force. Two sets rather than one: `only` is a
whitelist and `hide` a blacklist, and a directive has to move the right one or it appears to do
nothing.
"""

# the obc emits these every control cycle - four kinds at 10 Hz is forty lines a second, which
# scrolls anything worth reading off the screen before it can be read
PER_CYCLE_KINDS = ("IMU", "POWER", "TEMP", "CAMERA")

# hidden at startup on top of the per-cycle stream. GROUND is the ground station's own health once
# a second, and it is worth exactly nothing when the link is working and everything when it is
# not - so it stays available rather than deleted. `/show GROUND` is the first move when the
# payload link goes quiet, because a receiver that failed to initialise is otherwise invisible
QUIET_KINDS = PER_CYCLE_KINDS + ("GROUND",)

# every line kind the console can show. the local-echo kinds at the end never come off the wire
KINDS = (
    "HEARTBEAT",
    "ATTITUDE",
    "TASKS",
    "BOOT",
    "LORA",
    "NRF24",
    "IMU",
    "POWER",
    "TEMP",
    "CAMERA",
    "PAYLOAD",
    "DOWNLINK",
    "LINK",
    "REQUEST",
    "MISSION",
    "GROUND",
    "UART",
    "COMMAND",
    "COMMAND_ACK",
    "WHEEL_CMD",
    "WHEEL_STATUS",
    "TEXT",
    "LINKERR",
)

# the local side of the conversation is never filtered - hiding what you just typed because of an
# `only` set is how you sit there wondering why nothing happened
ALWAYS_SHOWN = ("SENT", "REJECT", "FILTER")

VERBS = ("all", "quiet", "only", "show", "hide", "filters")


class Filters:
    """The console's show/hide state, and the directives that change it."""

    def __init__(self, only: set | None = None, hide: set | None = None):
        self.only = set(only or ())
        self.hide = set(hide or ())

    @classmethod
    def quiet(cls) -> "Filters":
        """The startup default: everything except the per-cycle stream and the ground's own health."""
        return cls(hide=set(QUIET_KINDS))

    def visible(self, kind: str) -> bool:
        if kind in ALWAYS_SHOWN:
            return True
        if self.only:
            return kind in self.only
        return kind not in self.hide

    def apply(self, verb: str, kinds: set | None = None) -> bool:
        """Run one directive. Returns False if the verb is not one, leaving state untouched."""
        kinds = kinds or set()
        if verb == "all":
            self.only, self.hide = set(), set()
        elif verb == "quiet":
            self.only, self.hide = set(), set(QUIET_KINDS)
        elif verb == "only" and kinds:
            self.only, self.hide = set(kinds), set()
        elif verb == "show" and kinds:
            # widening, never narrowing: with an `only` set in force, removing a kind from it
            # would hide the thing just asked for. so drop it from `hide` and, if an `only` set
            # exists, add to it instead
            self.hide -= kinds
            if self.only:
                self.only |= kinds
        elif verb == "hide" and kinds:
            self.hide |= kinds
            self.only -= kinds
        elif verb == "filters":
            pass
        else:
            return False
        return True

    def describe(self) -> str:
        if self.only:
            return f"showing only {', '.join(sorted(self.only))}"
        return f"hiding {', '.join(sorted(self.hide))}" if self.hide else "showing everything"
