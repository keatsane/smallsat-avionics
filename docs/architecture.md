# Architecture

Flight behavior gets worked out in software before the STM32 has to prove it on the bench. Python drives declared test scenarios into the C++ flight logic, first on the host (SIL) and later against the real STM32 node over UART (HIL).

```text
Python scenario runner  (YAML scenarios: fault injection, grading, pass/fail reports)
        |
        v
C++ flight software     (modes, faults, commands, telemetry)
        |
        +----------------------------+
        v                            v
SIL shim (host exe,           STM32 HIL node
injected time + faults)       (bare-metal UART packets first; sensors and watchdog later)
```

On the SIL side, scenarios in `fsw/sil/scenarios/` drive the unmodified flight-software library through a small shim executable, and the runner grades the observed behavior and writes reports to `docs/reports/` (the details live in [verification.md](verification.md)). A physics plant model joins the harness when attitude dynamics matter - a single-axis rigid body with one reaction wheel, written in-repo, in the ADCS phase.

## Language split

| Area | Language | What it does |
| ---- | -------- | ------------ |
| STM32 firmware | C | CMSIS register-level startup, drivers, interrupts, timers, UART/SPI/I2C, ADC, GPIO, watchdog, low-level packet I/O |
| Flight software | C++ | mode manager, fault manager, sensor monitor, command validation, telemetry model, host unit tests |
| Test and analysis | Python | scenario execution, SIL/HIL orchestration, plotting, log parsing, reports |

The flight logic lives in `fsw/` and is written to be portable. It runs on the host for development and SIL, and the same source cross-compiles onto the STM32 for the integrated build, where it joins the firmware as the on-board computer. It never touches registers directly - it reaches hardware through a platform abstraction layer, backed by the simulator on the host and by the C firmware drivers on the target. The split is by layer, not by folder: the firmware owns the hardware (registers, peripherals, low-level I/O), the flight software owns the decisions (modes, faults, commands), and they meet at that boundary. The flight software allocates nothing dynamically - it uses fixed-capacity containers (ETL) so the same code runs on the host and on the no-heap target.

## Communication links

Everything between the satellite and the ground rides framed, CRC-checked packets (`common/protocol/`), but it is not one undifferentiated "telemetry" stream - there are two logical links with different jobs.

```text
   SATELLITE (OBC)                                   GROUND STATION (host PC / Teensy box)

   TT&C link - telemetry down + commands up, low-rate, always on (UART / LoRa RFM95)
        commands   <---------------------------------   SET_MODE, CLEAR_FAULT, NOOP, ...                (uplink)
        telemetry  --------------------------------->   heartbeat (mode + fault bitmask), link status, acks  (downlink)

   payload link - high-rate, only in DOWNLINK mode (nRF24 2.4 GHz)
        payload    --------------------------------->   bulk imaging data, chunked over a contact pass  (downlink)
```

The **TT&C link** (telemetry, tracking & command - the standard name for a spacecraft's low-rate housekeeping channel) carries commands up and housekeeping telemetry down. It is low-rate and stays on so the ground can always reach the spacecraft. The **payload link** is separate and high-rate; it powers up only in DOWNLINK mode to empty the imaging buffer over a contact pass, because a power-hungry high-rate radio cannot be always on. Both carry the same frame format - only the transport changes - so the codec is written once.

Telemetry and downlink are not the same thing. Telemetry is the continuous housekeeping beacon (mode, faults, health) on the TT&C link; "downlink" is the payload-emptying *mode* on the payload link. Both travel satellite-to-ground, but they are different content on different links.

### What the satellite can observe

The spacecraft only ever sees its own end of a link - it cannot hear its own transmissions, so it can never directly know whether the ground received a downlink. That asymmetry sets which link faults exist at all:

- Uplink silence is observable. If no valid command arrives within a timeout, a command-loss timer raises `COMMAND_LINK_LOSS` - the one onboard link-health fault, meaning ground contact is lost, detected purely from the command (uplink) half of the TT&C link going quiet.
- Downlink delivery is not observable onboard. Whether the ground is hearing the telemetry is detected on the ground, from gaps in the heartbeat sequence number - never by a fault on the satellite.
- Sequence numbers (`command_t.seq`, `heartbeat_t.seq`) detect drops, not loss - the receiving end spotting a missed or duplicate packet, a finer signal than the link being down.
- `uart_status_t` reports link quality (UART receive-line error counts: overrun, framing, noise, dropped) as diagnostic telemetry, not as a fault. Each transport gets its own status message as it lands (LoRa: RSSI/SNR, nRF24: retransmit counts), since the quality observables are transport-specific.
- The future payload link is the exception: the nRF24 radio auto-acknowledges, so the satellite can tell a payload packet was not received and raise a `PAYLOAD_LINK_LOSS` - but only once that hardware exists.

### Beacon rates and the loss timeout

Both directions beacon at 1 Hz: the satellite sends a heartbeat every second, and the ground sends a NOOP keep-alive every second. That is what NOOP is for - the command-loss timer only works if the ground is expected to keep talking. Either end declares link loss after 5 seconds of silence, enough missed beacons that a few dropped packets do not trip it. These are bench rates, chosen so a person watching the console can see the system breathe; a real spacecraft would beacon slower and tolerate much longer command silence, but the same logic applies with different constants.

## Onboard interfaces

Separate from the comm links above. Those carry the spacecraft-to-ground contract - framed, CRC-checked packets. The onboard interfaces are how the OBC reaches its own sensors on the satellite itself. This traffic never leaves the board and is not framed - each device speaks its own native register protocol, handled in its driver under `obc/`. Selected readings are then repackaged into downlink telemetry (for example `imu_data_t`) and sent to ground over a comm link, but the onboard bus read and the downlink packet are two different things: the read is a register access over SPI or I2C, the packet is a framed message over the link.

| Peripheral | Bus | How the OBC talks to it | Status |
| ---------- | --- | ----------------------- | ------ |
| ICM-20948 IMU (accel + gyro, plus the AK09916 magnetometer) | SPI2 | register read/write; the mag is polled through the IMU's own internal I2C master and read back in the same burst | on the bench |
| INA228 power monitor | I2C1 | register reads - bus voltage, current, and power, sensed high-side on the main battery bus | on the bench |
| TMP117 temperature | I2C1 | register reads - placeable structural temperature | on the bench |
| OV2640 camera (ArduCAM) | SPI3 + I2C1 | two buses at once: SCCB register writes over I2C to configure the sensor, SPI to read frames out of the onboard FIFO | answers on both buses; driver not written |
| B-G431B-ESC1 reaction-wheel driver | USART1 | framed torque commands out, wheel status back; the ESC closes the FOC loop locally against its own encoder | link proven; OBC-side driver not written |
| WS2812 status LEDs | GPIO (PA8) | one timed data line, three beads chained - mode, fault, and link | on the bench |

More peripherals join this table as their phases arrive.

The I2C bus runs at **100 kHz, not 400 kHz**. Once the slice, the harness, and three devices were all on it, the measured 10-90% rise time on SCL landed around 600-1000 ns - inside standard mode's 1000 ns budget but well past fast mode's 300 ns. The bus sits under 2% busy either way, so there was nothing to gain from pushing it.

## Fault handling and sensor monitoring

Fault detection, isolation, and recovery (FDIR) is one pipeline that every fault flows through: detect -> debounce -> latch -> respond. A condition is sampled each control cycle; a fault latches only after a set number of consecutive bad samples (debounce, so a single transient never trips it); once latched it stays latched until it is explicitly cleared; and its response is driven by its severity. The per-fault policy - severity, debounce threshold, owning requirement - lives in one table in the fault manager, so adding a fault is a table row, not new logic. The full catalog and the per-fault requirements are in [requirements.md](requirements.md).

Severity decides the response. A **Warning** is surfaced in telemetry and nothing else - off-nominal but still fully capable. A **Critical** fault forces SAFE if it does not clear. A **Degraded** fault sits between them: a capability was lost but a documented fallback exists, so the spacecraft switches to it and keeps operating rather than safing. The fallbacks are specified in requirements.md - for example, losing the accel/gyro path (`ACCEL_GYRO_DROPOUT`) while pointing or detumbling retreats the spacecraft to STANDBY, since holding attitude without body-rate feedback is unsafe. Losing only the magnetometer (`MAG_DROPOUT`) is a Warning: it is reported, but the spacecraft keeps flying.

Most faults are fed their samples from outside the flight software (the power monitor's voltage, the command-loss timer). Sensor faults are different: the **sensor monitor** is the detect stage for the onboard sensors, turning each cycle's readings into fault samples. It reads the validity flags the drivers attach to every sample - invalid or missing IMU data raises that source's dropout fault, and an invalid INA228 sample raises `POWER_DROPOUT` - and it also watches the values themselves. A live IMU reading always moves a little, so a stream that goes perfectly flat past a staleness window is treated as frozen and raises the same dropout fault. Staleness is a detector for a dropout, not a separate fault. A sensor that simply is not sampled on a given cycle gets no opinion, so a SIL scenario that never exercises a sensor cannot trip its faults. The monitor only detects; the response still happens in the executive's single fault-response step.

Recovery is deliberately conservative and ground-commanded: a latched fault clears only on a CLEAR_FAULT command, never autonomously, so a flapping sensor cannot toggle a fault on and off. A planned RESET_DEVICE command will let the ground re-initialize a misbehaving peripheral before deciding to clear its fault - both routed through the platform abstraction layer as actions the flight software performs, not inputs it reads.

## Firmware approach

Bare metal first: CMSIS startup, clock, a GPIO heartbeat, a SysTick or timer time base, interrupt-driven UART, then simple packet framing. FreeRTOS comes in when the workload turns genuinely concurrent - multiple rate-critical tasks (sensors, telemetry, commands, a control loop) - not before, when a super-loop is enough, and not as an afterthought.

That point arrived once the OBC was sampling four sensors, answering ground commands, and streaming a payload image over one link, all inside a single 10 Hz super-loop where any one blocking read delayed the rest. The task model keeps the concurrency at the platform boundary: **one control task** owns the flight software and calls `Executive::cycle()` exactly as the super-loop did, and every other job runs as its own task that hands work to it through a queue. That is deliberate rather than incidental - the executive being a pure function of its inputs is what lets the host unit tests and the SIL scenarios exercise the same code the target runs, and putting flight state behind mutexes instead would mean the host was testing a different concurrency model than the spacecraft. Mutexes are not ruled out; they guard shared *resources*, not shared decisions. The first one arrived with the second task that writes the console: the UART's transmit ring has one producer per writer task, and `uart_write` holds that lock for the whole of one write so two tasks cannot interleave their bytes into one unreadable frame. The control task runs at the highest priority, the kernel is configured for static allocation only (a dynamically created task or queue fails to link), and the SysTick driver keeps its vector and forwards each tick to the scheduler, so the millisecond time base every log entry is stamped with does not restart when the scheduler does.

The firmware is grouped by layer under `obc/`: `drivers/` (clock, gpio, systick, uart, spi, i2c) - register-level code for the MCU's own peripherals that moves raw bytes - and `devices/` (the ICM-20948 IMU, with the power and temperature sensors as they come up) - the external chips that ride those buses. The frame envelope and message definitions live in `common/protocol/` (C++) - the wire contract the flight software owns and the ground-station firmware will share when it lands. The protocol is the OBC's ground interface, not something the firmware interprets, so the flight software builds and parses the frames while the drivers just carry the bytes. A SysTick time base provides the millisecond clock, and an interrupt-driven multi-instance USART driver spans three UARTs - a console on USART2 (the ST-Link virtual COM port, for the host decoder and debug), a downlink on USART6 (a header pin, so it can be probed on a scope or logic analyzer and later swapped for a radio), and the ESC command link on USART1. The I2C driver is a polled master; bringing the INA228 up on the bench surfaced a documented F4 erratum - the cell leaves its STOP bit wedged after a transfer (es0298 2.11.3) - so each transfer clears that stale stop with a controller reset before it issues a START.

## Nodes

The OBC is not the only processor in the system, so each node that runs its own firmware gets its own top-level directory alongside `obc/` and `fsw/`.

| Path | Node | Runs on |
| ---- | ---- | ------- |
| `obc/` + `fsw/` | on-board computer - drivers, flight software | STM32F446 (Nucleo) |
| `esc/` | reaction-wheel driver - FOC, encoder, torque commands | STM32G431 (B-G431B-ESC1) |
| `gsw/` | ground station - link decode, display, command entry (planned) | Teensy |

Each node owns its build and toolchain: the OBC builds with CMake and the `arm-none-eabi` toolchain, which is both the image that gets flashed and the check CI runs, with STM32CubeIDE kept as a debugger rather than a second build; the ESC node is an Arduino sketch on top of SimpleFOC, which handles the current control loop and the AS5600 encoder locally. The OBC only sends it torque targets over UART, so the fast inner loop stays off the flight computer. What crosses between nodes is the wire contract in `common/protocol/`, which the ground-station firmware will share when it lands.

## Vendored dependencies

Third-party code lives under `vendor/`, never copied into the node trees. Two forms, by one rule: **libraries the build compiles against are submodules; constant data transcribed out of a library is a checked-in file with its provenance recorded.**

The distinction is about what upstream is for. A submodule pins a whole project at a commit and is worth the weight when the project is the thing being used. The OV2640 tables are four constant arrays lifted out of a 115 MB Arduino repository that is otherwise unused here - a submodule would carry the entire library, and its release cadence, to get 19 KB of register values that have not changed in years. Checked in, the file is still byte-exact and still diffable against upstream; `docs/setup.md` has the one-line refetch.

| Path | Form | What it is |
| ---- | ---- | ---------- |
| `vendor/cmsis-core` | submodule | CMSIS core headers and support |
| `vendor/cmsis-device-f4` | submodule | STM32F4 device headers and startup references |
| `vendor/FreeRTOS-Kernel` | submodule | the RTOS kernel - `tasks.c`, `queue.c`, `list.c`, and the GCC ARM_CM4F port are in the OBC image; nothing else is |
| `vendor/etl` | submodule | the Embedded Template Library - fixed-capacity, no-heap containers for the flight software |
| `vendor/arducam` | checked in | OmniVision's OV2640 register tables by way of ArduCAM's library, plus the small shim that lets them compile outside Arduino |

Everything under `vendor/` is excluded from the formatters, so vendored files stay byte-identical to upstream.

## Stack architecture - three-plate stack on the lazy-susan (revised 2026-07-17, supersedes the four-wall cube)

Three plates on standoffs, stacked and spinning as one on the lazy-susan bearing below - bottom to top: **gimbal plate, compute plate, comms plate**. This supersedes the four-wall-cube-around-a-central-battery plan (2026-06-25): in the actual build the electronics consolidated onto a single middle plate instead of four separate walls. Plates stack on corner standoffs with open sides; wires run straight up through pass-through holes left in each plate. (Harnessing is per-plate, and the single morpho slice rides the Nucleo on the compute plate - see wiring.md for the pin map, the ten connectors, and the disconnect points.)

**Gimbal plate (bottom):** the reaction-wheel motor (GBM4108-120T) with the flywheel on it, the AS5600 encoder on the motor shaft end below, and the lazy-susan bearing under the plate. The motor stator bolts to this plate; the flywheel is the free-spinning reaction wheel, and the whole stack counter-rotates on the bearing. Coaxial with the Z pivot axis - the movement layer (the tolerances are in [hardware.md](hardware.md)'s placement rules).

**Compute plate (middle):** every electronic part on one plate.
- **Sensor protoboard (7x9 cm):** IMU (ICM-20948), TMP117, ArduCAM OV2640, and the INA228 (all soldered on 2026-06-25; the INA's shunt is inline with the 3V3 feed reading logic-rail draw for now - moves to the battery feed for total draw at Phase 8). Camera at the outer edge behind a window; TMP reachable (you warm it for the overtemp demo) and away from buck heat. The IMU can read yaw rate from anywhere on the rigid plate (future to-do: an axis-remap so its mounted orientation reads as yaw in ADCS) - but keep it as far from the ESC and the motor below as the plate allows (see EMI).
- **OBC:** the Nucleo-F446RE, the hub that fans out to everything. Its **USB edge must face a frame opening** for flash/debug. On standoffs; signals leave off the Morpho pins.
- **ESC:** the B-G431B-ESC1, bolted down by its own holes (no protoboard). Wire it directly: battery power in, the 3 motor phases down to the gimbal plate, the AS5600 I2C (short, to the encoder below), and the control link (USART1 - TX PA9, RX PA10, GND - to the OBC).
- **Power:** battery -> **master switch + fuse** (LiPo safety) -> **INA228** (at the main bus for total battery draw, Phase 8) -> distribution to the 5 V buck, the 3V3 buck (radio rail), and the ESC feed. The single-point (**star) ground** lives here; both bucks share it. Battery-input protection part specs are under Power architecture below.

**Comms plate (top, Phase 8):** the two radios (RFM95 LoRa + nRF24L01), the status LED(s), and the antennas. Not built yet - the plate is reserved with wire pass-through holes so SPI3 + control + a dedicated 3V3 feed run up from the compute plate later. Each radio's **SMA whip bends 90 deg and lies flat along a top edge**, the pair tracing the perimeter so nothing pokes out; the **LED(s) sit centre**, visible from above at any spin angle. The LED is the one comms-plate part wired now (3V3, GND, DIN PA8).

**Battery (Phase 8, placement open):** not in the current build - the bench PSU stands in. Where the 4S LiPo mounts is the main open mechanical question, and mass balance decides it: it's the heaviest item, so its position sets whether the rotating CG lands on the Z axis (the mass-balance master rule under Placement rules). Likely a central column through the plates (as in the old plan) or low under the compute plate with a counterweight - settle it when the untethered build starts, together with the master-switch mounting. The rocker is a panel part, not a plate part - it doesn't compete for plate space; a small printed bezel on the frame rails near the base is preferred (short high-current lead, clear of the antennas), with the top plate an acceptable fallback if the base can't host it (at the cost of an up-and-down battery lead and keeping the fat DC wires away from the antenna whips).

**Buses (shared, multi-drop):** **I2C1** (SDA PB9, SCL PB8) carries TMP + INA + the camera's SCCB; **SPI3** (SCK PC10, MISO PC11, MOSI PC12) carries the camera + LoRa + nRF24, each with its own CS/control; the **IMU** sits alone on **SPI2** (PB13/14/15, CS PB12). On the compute plate the sensor protoboard cables to the Nucleo through three connectors: (1) power + I2C - 3V3/GND/SDA/SCL; (2) IMU SPI2 - GND/SCK/MISO/MOSI/CS; (3) camera SPI3 - GND/SCK/MISO/MOSI/CS (SPI3 also forks up to the radios on the comms plate at Phase 8).

**EMI - the cost of consolidating (watch this):** the four-wall plan deliberately put the sensors opposite the ESC, because the ESC's phase switching is the worst noise source and the IMU's magnetometer the most sensitive part. The 3-plate stack drops them onto the same middle plate, and the IMU now also sits directly above the motor's magnets on the gimbal plate - so that separation is gone and has to be won back by layout:
- On the compute plate, put the IMU at the far edge/corner, as far from the ESC and the motor's Z axis as the board allows; cluster the ESC and its fat phase wires at the opposite side.
- Maximize the IMU's vertical gap and lateral offset from the motor/flywheel below (motor magnets corrupt the magnetometer - the placement rules want 30-50 mm+).
- Route motor phase wires and battery leads hugging the ESC side, never past the IMU or the INA228 sense.
This is the main technical tradeoff of the consolidation; the keep-away matrix in [hardware.md](hardware.md) still governs, it is just harder to satisfy on one plate.
