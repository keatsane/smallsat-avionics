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
The host flight software is unit-tested and running through an eight-scenario SIL suite: modes, faults, command validation, telemetry, recovery, and same-cycle response ordering. The same C++ flight logic cross-compiles onto the STM32 behind a platform abstraction; the bare-metal side has SysTick timing, interrupt-driven UART on USART2/USART6, and the shared framed message codec in `common/protocol/`.

The first HIL slice is bench-verified: the board beacons live telemetry, the host runner detects link loss against the 5 s heartbeat timeout, and both HIL scenarios pass with scope-backed packet timing. The ICM-20948 IMU is also in the loop: accel/gyro plus AK09916 magnetometer samples go out as timestamped telemetry with per-half validity flags, and the sensor monitor turns invalid or frozen sources into the matching dropout faults. `ACCEL_GYRO_DROPOUT` is Degraded and can retreat POINTING/DETUMBLE to STANDBY; `MAG_DROPOUT` is a report-only Warning. INA228 power telemetry is now live on the bench - bus voltage, current, and power go out as timestamped, validity-flagged samples, with the current reading verified against the bench draw; `POWER_DROPOUT` covers an invalid or missing monitor sample. Structural temperature (TMP117) is on the bench too, streaming as validity-flagged telemetry, with overtemperature wired to SAFE and a report-only undertemperature; the imaging payload (ArduCAM) is next.

## Layout
- bsp/ - STM32 board-support firmware (drivers, startup, board bring-up)
- common/ - the wire contract: frame codec, message layouts, and the command/fault/mode id catalog
- docs/ - architecture, setup, requirements, scenarios, V&V, reports, and the bill of materials
- fsw/ - portable C++ flight software: modes, faults, command handling, with host unit tests and the SIL shim
- tools/ - host-side scripts (telemetry monitor, SIL and HIL scenario runners, scope capture) over a shared ground library
- vendor/ - CMSIS, FreeRTOS, and ETL, vendored as Git submodules

The simulation directory is added as that part comes together.

## License
Apache 2.0.
