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

5. Real sensors (IMU, voltage/current, temperature) and an imaging payload, with faults driven by real hardware.
6. A FreeRTOS task model once the workload turns concurrent, plus a demonstrated watchdog reset.

Remaining work is a single-axis reaction wheel that detumbles and points the platform - run both against NASA's open-source 42 simulator and on the physical rig - and then the untethered capstone, with telemetry over a radio link.

## Status
The host flight software is unit-tested and running through a ten-scenario SIL suite: modes, faults, command validation, telemetry, recovery, degraded fallbacks, and same-cycle response ordering. The same C++ flight logic cross-compiles onto the STM32 behind a platform abstraction; the bare-metal side has SysTick timing, interrupt-driven UART across three USARTs, and the shared framed message codec in `common/protocol/`.

The OBC now runs a FreeRTOS task model rather than a super-loop: one control task owns the flight software at 10 Hz on an absolute wake-up, sensor sampling and health reporting run as their own tasks, and every task's state, check-in age, and stack high-water go out as telemetry at 1 Hz. An independent watchdog is serviced only when every task with a liveness deadline has checked in, and its bite has been demonstrated on the bench - the board resets and comes back reporting `reset=iwdg-watchdog`.

The first HIL slice is bench-verified: the board beacons live telemetry, the host runner detects link loss against the 5 s heartbeat timeout, and both HIL scenarios pass with scope-backed packet timing. The ICM-20948 IMU is also in the loop: accel/gyro plus AK09916 magnetometer samples go out as timestamped telemetry with per-half validity flags, and the sensor monitor turns invalid or frozen sources into the matching dropout faults. `ACCEL_GYRO_DROPOUT` is Degraded and can retreat POINTING/DETUMBLE to STANDBY; `MAG_DROPOUT` is a report-only Warning. INA228 power telemetry is live too - bus voltage, current, and power as timestamped, validity-flagged samples, sensed high-side on the main battery bus so the voltage reads state of charge; `POWER_DROPOUT` covers an invalid or missing sample. Structural temperature (TMP117) streams the same way, with overtemperature wired to SAFE and a report-only undertemperature.

The physical rig is now built: a three-plate stack on a lazy-susan pivot, with the reaction wheel and its encoder below, the OBC and sensors in the middle, and the radios and status LEDs up top. The wheel runs closed-loop FOC against a real magnetic encoder, and stepping its speed visibly kicks the platform the other way - the reaction-wheel effect, on the bench. The whole stack runs off a 14.8 V bus through its own switch, shunt, and regulators. The imaging payload answers on both of its buses, and the radios and LED array are wired.

## Layout
- obc/ - on-board computer firmware, STM32F446 board support (drivers, startup, board bring-up)
- common/ - the wire contract: frame codec, message layouts, and the command/fault/mode id catalog
- docs/ - the written record: architecture, requirements, verification, hardware, and reports
- esc/ - reaction-wheel node firmware, SimpleFOC on the B-G431B-ESC1
- fsw/ - portable C++ flight software: modes, faults, command handling, with host unit tests and the SIL shim
- tools/ - host-side scripts (telemetry monitor, SIL and HIL scenario runners, scope capture) over a shared ground library
- cad/ - Fusion 360 models for the printed frame, plates, and flywheel
- vendor/ - CMSIS, FreeRTOS, and ETL, vendored as Git submodules

The simulation directory is added as that part comes together.

## Documentation

| Document | What is in it |
| -------- | ------------- |
| [architecture.md](docs/architecture.md) | how the system is put together: the language split, the two comm links, the onboard buses, the FDIR pipeline, and the three-plate stack |
| [requirements.md](docs/requirements.md) | every requirement with its id, status, verification method, and the artifact that proves it |
| [verification.md](docs/verification.md) | the evidence ladder, the SIL and HIL harnesses, and the full scenario catalog |
| [setup.md](docs/setup.md) | prerequisites, building and flashing each node, and every test and tooling command |
| [bom.md](docs/bom.md) | the parts actually on the bench, with a vendored datasheet block for each |
| [hardware.md](docs/hardware.md) | the physical build: part constraints, bench tools, placement and keep-away rules, and the CAD layout |
| [wiring.md](docs/wiring.md) | the bench wiring sheet - connector by connector, pin by pin, in build order |
| [roadmap.md](docs/roadmap.md) | a working document: what is left to build, in order, and the design decisions behind it |
| [journal.md](docs/journal.md) | a working document: the dated log of what was built each session and how it went |
| [reports/](docs/reports) | generated SIL and HIL run reports, plus the scope-capture companions |

## License
Apache 2.0.
