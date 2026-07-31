# SmallSat Avionics SIL/HIL Testbed

A bench testbed for small-satellite avionics: STM32 firmware, portable C++ flight logic, and Python tooling for software- and hardware-in-the-loop testing.
Everything runs on a workbench with real parts and test gear: an STM32 as the flight computer, sensors and power hardware, a logic analyzer, and a scope. Behavior gets checked in software first (SIL), then on the hardware itself (HIL), and the results - logs, reports, and scope captures - land in the repo alongside the code.

## The stack
- STM32 firmware in C: bare-metal CMSIS drivers, timers, UART, sensors, watchdog.
- Flight logic in C++: spacecraft modes, fault detection, and safe-mode handling, written to run and be tested on a PC before reaching hardware.
- Python: test scenarios, fault injection, and report generation.

## Status
The mission loop works over radio, end to end: the vehicle - on bench power, no data cable - is commanded over a LoRa uplink into its pointing mode, captures an image, and downlinks it over a separate nRF24 payload link, where the ground station reassembles it and asks for whatever the air dropped. The lossy one-way payload link runs selective repeat: one full pass, then the ground names its missing chunks over the uplink and the vehicle resends exactly those out of the camera's own buffer.

The host flight software is unit-tested and runs a fourteen-scenario SIL suite: modes, faults, command validation, telemetry, recovery, degraded fallbacks, same-cycle response ordering, and closed-loop attitude scenarios against an in-repo plant model with measured inertias. The same C++ flight logic cross-compiles onto the STM32 behind a platform abstraction; the bare-metal side has SysTick timing, interrupt-driven UART across three USARTs, register-level SPI and I2C drivers for six devices, and the shared framed message codec in `common/protocol/`.

The OBC now runs a FreeRTOS task model rather than a super-loop: one control task owns the flight software at 10 Hz on an absolute wake-up, sensor sampling and health reporting run as their own tasks, and every task's state, check-in age, and stack high-water go out as telemetry at 1 Hz. An independent watchdog is serviced only when every task with a liveness deadline has checked in, and its bite has been demonstrated on the bench - the board resets and comes back reporting `reset=iwdg-watchdog`.

The first HIL slice is bench-verified: the board beacons live telemetry, the host runner detects link loss against the 5 s heartbeat timeout, and both HIL scenarios pass with scope-backed packet timing. The ICM-20948 IMU is also in the loop: accel/gyro plus AK09916 magnetometer samples go out as timestamped telemetry with per-half validity flags, and the sensor monitor turns invalid or frozen sources into the matching dropout faults. `ACCEL_GYRO_DROPOUT` is Degraded and can retreat POINTING/DETUMBLE to STANDBY; `MAG_DROPOUT` is a report-only Warning. INA228 power telemetry is live too - bus voltage, current, and power as timestamped, validity-flagged samples, sensed high-side on the main battery bus so the voltage reads state of charge; `POWER_DROPOUT` covers an invalid or missing sample. Structural temperature (TMP117) streams the same way, with overtemperature wired to SAFE and a report-only undertemperature.

The physical rig is built: a three-plate stack on a lazy-susan pivot, with the reaction wheel and its encoder below, the OBC and sensors in the middle, and the radios and status LEDs up top. The wheel runs closed-loop FOC against a real magnetic encoder, and stepping its speed visibly kicks the platform the other way - the reaction-wheel effect, on the bench. The whole stack runs off a 14.8 V bus through its own switch, shunt, and regulators.

The attitude layer runs on the platform even while the wheel's driver board is away for replacement: yaw comes from a gyro-integrated, magnetometer-anchored heading estimate (the compass self-calibrates its hard iron and even its sign, live, against the gyro), DETUMBLE enters autonomously when the platform is spun and hands back whatever mode it interrupted, and a commanded bearing survives mode changes. The ground segment is its own node: an Adafruit Feather M0 with both receivers and two OLED status panels, compiling the satellite's own protocol headers, bridged to a Python console that decodes, files images, retries commands until acknowledged, and can fly the whole point-shoot-downlink-park sequence as one `shoot` command.

Remaining: the wheel driver's replacement board (closing the physical control loop, with HIL for the attitude requirements), the battery and its fuse (removing the last wires entirely), and the slew-and-image survey capstone.

## Layout
- obc/ - on-board computer firmware, STM32F446 board support (drivers, devices, FreeRTOS tasks)
- common/ - the wire contract: frame codec, message layouts, and the command/fault/mode id catalog
- docs/ - the written record: architecture, requirements, verification, hardware, and reports
- esc/ - reaction-wheel node firmware, SimpleFOC on the B-G431B-ESC1
- fsw/ - portable C++ flight software: modes, faults, commands, attitude control, with host unit tests, the SIL shim, and the plant model
- gsw/ - ground station firmware for the Feather M0: both receivers, the uplink, and the OLED panels
- tools/ - host-side Python: the ground console, SIL and HIL scenario runners, scope capture, over a shared ground library
- cad/ - Fusion 360 models for the printed frame, plates, and flywheel
- vendor/ - CMSIS, FreeRTOS, and ETL as submodules, plus checked-in OV2640 register tables

## Documentation

| Document | What is in it |
| -------- | ------------- |
| [architecture.md](docs/architecture.md) | how the system is put together: the language split, the two comm links, the onboard buses, the FDIR pipeline, and the three-plate stack |
| [requirements.md](docs/requirements.md) | every requirement with its id, status, verification method, and the artifact that proves it |
| [verification.md](docs/verification.md) | the evidence ladder, the SIL and HIL harnesses, and the full scenario catalog |
| [setup.md](docs/setup.md) | prerequisites, building and flashing each node, and every test and tooling command |
| [bom.md](docs/bom.md) | the parts actually on the bench, with a vendored datasheet block for each |
| [hardware.md](docs/hardware.md) | the physical build: part constraints, bench tools, placement and keep-away rules, and the CAD layout |
| [wiring.md](docs/wiring.md) | the bench wiring sheet - connector by connector, pin by pin |
| [roadmap.md](docs/roadmap.md) | a working document: what is left to build, in order, and the design decisions behind it |
| [journal.md](docs/journal.md) | a working document: the dated log of what was built each session and how it went |
| [reports/](docs/reports) | generated SIL and HIL run reports, plus the scope-capture companions |

## License
Apache 2.0.
