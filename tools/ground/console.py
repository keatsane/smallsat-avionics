"""The interactive half of the ground console.

Kept separate from the monitor because it is the part with a hard dependency on prompt_toolkit,
and because piping commands in is a genuinely different mode from typing them: a pinned prompt
needs a terminal, a script does not.
"""

from ground.commands import ARG_CATALOG
from ground.frames import COMMANDS

PROMPT = "cmd> "


def _require_prompt_toolkit():
    try:
        from prompt_toolkit import PromptSession, print_formatted_text  # noqa: PLC0415
        from prompt_toolkit.completion import Completer, Completion  # noqa: PLC0415
        from prompt_toolkit.formatted_text import ANSI  # noqa: PLC0415
        from prompt_toolkit.history import InMemoryHistory  # noqa: PLC0415
        from prompt_toolkit.patch_stdout import patch_stdout  # noqa: PLC0415
    except ImportError:
        raise SystemExit(
            "the interactive console needs prompt_toolkit:  pip install prompt_toolkit\n"
            "or run with --read-only to just watch, or pipe commands in to script it"
        )
    return (
        PromptSession,
        print_formatted_text,
        ANSI,
        Completer,
        Completion,
        InMemoryHistory,
        patch_stdout,
    )


def make_session():
    """A prompt session, an ANSI-aware printer for it, and the stdout patch it needs.

    The printer matters: prompt_toolkit owns the terminal while the prompt is up, and raw escape
    codes written past it come out as literal `?[1;36m` rather than color. Its own printer
    renders them, on Windows too.
    """
    (
        PromptSession,
        print_formatted_text,
        ANSI,
        Completer,
        Completion,
        InMemoryHistory,
        patch_stdout,
    ) = _require_prompt_toolkit()

    class CommandCompleter(Completer):
        def get_completions(self, document, complete_event):
            text = document.text_before_cursor
            parts = text.split()
            # completing the first word unless a space has already closed it
            if len(parts) <= 1 and not text.endswith(" "):
                stem = parts[0].upper() if parts else ""
                for name in COMMANDS:
                    if name.startswith(stem):
                        yield Completion(name, start_position=-len(stem))
                return

            catalog = ARG_CATALOG.get(parts[0].upper())
            if catalog is None:
                return  # this command takes no argument - nothing to offer
            stem = parts[1].upper() if len(parts) > 1 else ""
            for name in catalog:
                if name.startswith(stem):
                    yield Completion(name, start_position=-len(stem))

    def emit(line: str) -> None:
        print_formatted_text(ANSI(line))

    session = PromptSession(PROMPT, completer=CommandCompleter(), history=InMemoryHistory())
    return session, emit, patch_stdout
