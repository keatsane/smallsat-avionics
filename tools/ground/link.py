"""Opening the serial link to a node.

One place so every bench tool fails the same way. Two failures look identical through pyserial and
are not: a port that does not exist means the wrong name, and a port that exists but will not open
means something else already holds it - serial ports are exclusive, so a monitor running in another
terminal locks out everything else. Naming which one it is saves the guess.
"""

import serial
from serial.tools import list_ports


def describe_ports() -> str:
    """Available ports with their descriptions, one per line, or a note that there are none."""
    ports = sorted(list_ports.comports(), key=lambda p: p.device)
    if not ports:
        return "  (none - is the board plugged in?)"
    return "\n".join(f"  {p.device}  {p.description}" for p in ports)


def find_port(stlink: str | None = None) -> str:
    """The node's serial port, by ST-Link serial number if given, else the only one present.

    Both boards enumerate as ST-Link virtual COM ports, so with the stack fully plugged in there
    is no way to tell them apart by description - and monitoring the wrong one is the same class
    of mistake as flashing the wrong one. The ST-Link serial that pins the flasher pins this too.
    """
    ports = list_ports.comports()
    if stlink:
        for p in ports:
            if (p.serial_number or "").upper() == stlink.upper():
                return p.device
        raise SystemExit(f"no port with st-link serial {stlink}\navailable:\n{describe_ports()}")

    if len(ports) == 1:
        return ports[0].device
    if not ports:
        raise SystemExit("no serial ports found - is the board plugged in?")
    raise SystemExit(
        f"more than one port - name one, or pin it by st-link serial:\n{describe_ports()}"
    )


def open_port(port: str, baud: int, timeout: float = 0.05) -> serial.Serial:
    """Open a node's serial link, or exit explaining which way it failed."""
    try:
        return serial.Serial(port, baud, timeout=timeout)
    except serial.SerialException as e:
        exists = any(p.device.upper() == port.upper() for p in list_ports.comports())
        if exists:
            raise SystemExit(
                f"{port} exists but will not open - something else is already using it.\n"
                f"a serial port takes one program at a time, so close any monitor on {port} "
                f"first (this tool prints telemetry too, so you do not need both).\n"
                f"underlying error: {e}"
            )
        raise SystemExit(f"no such port {port}\navailable:\n{describe_ports()}")
