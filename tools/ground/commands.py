"""Turning a typed command into the bytes that go on the wire.

Names, not numbers, resolved against the same catalogs the firmware is generated from - so a
typo is refused on the ground instead of arriving as a valid-looking wrong id. Shared by the
one-shot sender and the interactive monitor so both accept exactly the same syntax.
"""

from ground.frames import COMMANDS, FAULTS, MODES

# which catalog a command's argument is drawn from; absent means the command takes no argument
ARG_CATALOG = {"SET_MODE": MODES, "CLEAR_FAULT": FAULTS}


class CommandError(ValueError):
    """A command that does not name a real command, or a bad argument for one that does."""


def resolve(command: str, arg: str | None) -> tuple[int, int]:
    """Command name + argument name -> the (cmd_id, arg) pair that goes on the wire."""
    command = command.upper()
    if command not in COMMANDS:
        raise CommandError(f"unknown command {command!r} - one of: {', '.join(COMMANDS)}")

    catalog = ARG_CATALOG.get(command)
    if catalog is None:
        if arg is not None:
            raise CommandError(f"{command} takes no argument")
        return COMMANDS.index(command), 0

    if arg is None:
        raise CommandError(f"{command} needs an argument - one of: {', '.join(catalog)}")
    arg = arg.upper()
    if arg not in catalog:
        raise CommandError(f"unknown {command} argument {arg!r} - one of: {', '.join(catalog)}")
    return COMMANDS.index(command), catalog.index(arg)


def parse(line: str) -> tuple[int, int]:
    """One typed line ('SET_MODE DETUMBLE') -> the (cmd_id, arg) pair that goes on the wire."""
    parts = line.split()
    if not parts:
        raise CommandError("empty command")
    if len(parts) > 2:
        raise CommandError("expected at most 'COMMAND ARGUMENT'")
    return resolve(parts[0], parts[1] if len(parts) > 1 else None)


def usage() -> str:
    """Multi-line summary of what can be typed, for an interactive prompt's help."""
    lines = []
    for name in COMMANDS:
        catalog = ARG_CATALOG.get(name)
        lines.append(f"  {name} <{'|'.join(catalog)}>" if catalog else f"  {name}")
    return "\n".join(lines)
