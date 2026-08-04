#!/usr/bin/env python3
"""Find the platform's breakaway torque, using the wheel as the instrument.

The bearing does not move until the applied torque exceeds its static friction, and that number
decides what POINTING can and cannot do - but it is not in any datasheet. It is a property of this
bearing, under this load, today. The plant model's value is a guess (1.25x a kinetic figure fitted
from one coast-down), and every pointing gain has been chosen against that guess.

This measures it by pulsing the wheel at a rising torque and watching for the platform to move.
The step where it first moves is the answer.

    just esc-breakaway               # 1 mN m steps from 4, half-second pulses
    just esc-breakaway --from 10 --to 24 --step 0.5

Two things make this the right measurement rather than a convenient one:

- It reports the number in the units the flight software commands, not in physical N m. The
  torque-to-volts conversion carries an estimated motor constant (kOhmsPerKt), so a breakaway
  measured this way absorbs that error instead of inheriting it. What comes out is directly the
  torque the controller must ask for to move the platform.
- It pulses rather than ramps. The wheel saturates in about a fifth of a second at full torque,
  so a long push stops delivering torque partway through and a ramp would find the wrong answer.

The esc's UART reaches both its USB port and the obc harness, so unplug the 3-wire obc link before
running this - otherwise two transmitters fight over one wire. Watch the platform itself, or the
ground station's attitude readout over the radio, and note the step where it first turns.
"""

import argparse
import sys
import time

from ground.frames import encode_wheel_command
from ground.link import find_port, open_port

# torque -> q-axis volts, the same constant the flight software uses (platform_stm32.cpp). it is an
# estimate, and that is fine here: this tool reports the torque the controller has to *command*,
# so both ends carry the same error and it cancels
OHMS_PER_KT = 124.0


def main() -> int:
    ap = argparse.ArgumentParser(description="measure breakaway torque with the reaction wheel")
    ap.add_argument("--port", help="serial port; omit to find the esc automatically")
    ap.add_argument("--stlink", help="pin the port by ST-Link serial number")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--from", dest="start", type=float, default=4.0, help="first step, mN m")
    ap.add_argument("--to", dest="stop", type=float, default=30.0, help="last step, mN m")
    ap.add_argument("--step", type=float, default=1.0, help="step size, mN m")
    ap.add_argument("--pulse", type=float, default=0.5, help="seconds of torque per step")
    ap.add_argument("--rest", type=float, default=2.5, help="seconds at zero between steps")
    args = ap.parse_args()

    if args.step <= 0.0 or args.stop < args.start:
        ap.error("need a positive --step and --to at or above --from")

    seq = 0

    def send(port, torque_mnm: float) -> None:
        nonlocal seq
        seq = (seq + 1) & 0xFFFF
        volts = (torque_mnm / 1000.0) * OHMS_PER_KT
        port.write(encode_wheel_command(int(volts * 1000.0), seq))

    print("platform free, wheel stopped, obc link unplugged. watch for the first movement.")
    print(f"{'mN m':>8}  {'volts':>7}")

    with open_port(args.port or find_port(args.stlink), args.baud) as ser:
        torque = args.start
        while torque <= args.stop + 1e-9:
            print(f"{torque:8.1f}  {(torque / 1000.0) * OHMS_PER_KT:7.2f}", flush=True)

            # the node coasts if it hears nothing for 500 ms, so a held pulse has to be resent
            until = time.monotonic() + args.pulse
            while time.monotonic() < until:
                send(ser, torque)
                time.sleep(0.1)

            # back to zero and let the wheel spin down before the next step, so each pulse starts
            # from the same place. a step that inherits the last one's wheel speed is measuring
            # saturation, not friction
            send(ser, 0.0)
            time.sleep(args.rest)

            torque += args.step

    print("\nno more steps. note the lowest one that moved the platform - that is the breakaway")
    print("torque POINTING has to clear, in the units the flight software commands.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
