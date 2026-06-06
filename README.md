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
The bare-metal firmware is up and verified on the bench: a SysTick millisecond time base, a heartbeat LED, an interrupt-driven USART2 driver, and a CRC-checked frame layer streaming heartbeat and link-status packets over the Nucleo's virtual COM port, decoded by a host script in `tools/`. The C++ flight software is underway: a portable mode/fault-manager project that builds and unit-tests on the host (CMake + doctest), with the protocol codec now shared between the firmware and the flight software. The fault-handling logic is being filled in.

## Layout
- bsp/ - STM32 board-support firmware (drivers, startup, board bring-up)
- common/ - protocol code shared by the firmware and the flight software
- docs/ - architecture, setup, requirements, and the bill of materials
- fsw/ - portable C++ flight software (modes, faults) with host unit tests
- tools/ - host-side scripts (telemetry monitor)
- vendor/ - CMSIS, FreeRTOS, and ETL, vendored as Git submodules

The simulation directory is added as that part comes together.

## License
Apache 2.0.
