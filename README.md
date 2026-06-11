# SmallSat Avionics SIL/HIL Testbed

A bench testbed for small-satellite avionics: STM32 firmware, portable C++ flight logic, and Python tooling for software- and hardware-in-the-loop testing.
Everything runs on a workbench with real parts and test gear: an STM32 as the flight computer, sensors and power hardware, a logic analyzer, and a scope. Behavior gets checked in software first (SIL), then on the hardware itself (HIL), and the results - logs, reports, and scope captures - land in the repo alongside the code.

## The stack
- STM32 firmware in C: bare-metal CMSIS drivers, timers, UART, sensors, watchdog.
- Flight logic in C++: spacecraft modes, fault detection, and safe-mode handling, written to run and be tested on a PC before reaching hardware.
- Python: test scenarios, fault injection, and report generation.

## Roadmap
1. Bare-metal STM32: a heartbeat LED, a timer time base, then UART telemetry.
2. C++ mode and fault managers, with host unit tests.
3. First SIL demo: a scenario drops the bus voltage and the flight software enters SAFE, with a pass/fail report.
4. First HIL demo: the STM32 streams heartbeat packets over UART and the host detects a dropped link.

Later work adds real sensors (IMU, voltage/current, temperature), a FreeRTOS task model once the workload turns concurrent, a watchdog reset demonstration, and a single-axis reaction wheel that detumbles and points the platform - run both against NASA's open-source 42 simulator and on the physical rig, eventually untethered with telemetry over a radio link.

## Status
The bare-metal firmware is up and verified on the bench: a SysTick millisecond time base, a heartbeat LED, and interrupt-driven UART drivers on USART2 and USART6 - frame streaming was proven over the Nucleo's virtual COM port and on a scope, decoded by a host script in `tools/`, before the codec moved into the flight software (it returns as the flight software's telemetry path). The C++ flight software is complete at the unit level and working in SIL: the mode manager, fault manager, command validation with a command-loss timer, telemetry producer, and the executive control cycle, all unit-tested on the host (CMake + doctest). The first SIL slice is in: a scenario runner drives the flight software from declared YAML scenarios through a small shim executable, grades the observed behavior against expectations, and writes pass/fail reports to `docs/reports/` - the first proven scenarios are an undervoltage fault driving SAFE mode and an illegal command being rejected and acknowledged. The framing and message codec lives in `common/protocol/` as the wire contract between the spacecraft and the ground - the flight software builds and parses the frames while the firmware drivers just move bytes, and the ground-station firmware shares the same headers when it lands.

## Layout
- bsp/ - STM32 board-support firmware (drivers, startup, board bring-up)
- common/ - the wire contract: frame codec, message layouts, and the command/fault/mode id catalog
- docs/ - architecture, setup, requirements, scenarios, V&V, reports, and the bill of materials
- fsw/ - portable C++ flight software: modes, faults, command handling, with host unit tests and the SIL shim
- tools/ - host-side scripts (telemetry monitor, SIL scenario runner)
- vendor/ - CMSIS, FreeRTOS, and ETL, vendored as Git submodules

The simulation directory is added as that part comes together.

## License
Apache 2.0.
