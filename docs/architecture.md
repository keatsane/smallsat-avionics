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

On the SIL side, scenarios in `fsw/sil/scenarios/` drive the unmodified flight-software library through a small shim executable, and the runner grades the observed behavior and writes reports to `docs/reports/` (the details live in [vv.md](vv.md)). A physics plant model joins when real sensor and attitude dynamics matter - NASA's 42 simulator in the ADCS phase.

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

Separate from the comm links above. Those carry the spacecraft-to-ground contract - framed, CRC-checked packets. The onboard interfaces are how the OBC reaches its own sensors on the satellite itself. This traffic never leaves the board and is not framed - each device speaks its own native register protocol, handled in its driver under `bsp/`. Selected readings are then repackaged into downlink telemetry (for example `imu_data_t`) and sent to ground over a comm link, but the onboard bus read and the downlink packet are two different things: the read is a register access over SPI or I2C, the packet is a framed message over the link.

| Peripheral | Bus | How the OBC talks to it | Status |
| ---------- | --- | ----------------------- | ------ |
| ICM-20948 IMU (accel + gyro, plus the AK09916 magnetometer) | SPI2 | register read/write; the mag is polled through the IMU's own internal I2C master and read back in the same burst | on the bench |
| INA228 power monitor | I2C | register reads - bus voltage, current, and power | on the bench (current verified) |
| TMP117 temperature | I2C | register reads - placeable structural temperature | on the bench |

More peripherals join this table as their phases arrive.

## Fault handling and sensor monitoring

Fault detection, isolation, and recovery (FDIR) is one pipeline that every fault flows through: detect -> debounce -> latch -> respond. A condition is sampled each control cycle; a fault latches only after a set number of consecutive bad samples (debounce, so a single transient never trips it); once latched it stays latched until it is explicitly cleared; and its response is driven by its severity. The per-fault policy - severity, debounce threshold, owning requirement - lives in one table in the fault manager, so adding a fault is a table row, not new logic. The full catalog and the per-fault requirements are in [requirements.md](requirements.md).

Severity decides the response. A **Warning** is surfaced in telemetry and nothing else - off-nominal but still fully capable. A **Critical** fault forces SAFE if it does not clear. A **Degraded** fault sits between them: a capability was lost but a documented fallback exists, so the spacecraft switches to it and keeps operating rather than safing. The fallbacks are specified in requirements.md - for example, losing the accel/gyro path (`ACCEL_GYRO_DROPOUT`) while pointing or detumbling retreats the spacecraft to STANDBY, since holding attitude without body-rate feedback is unsafe. Losing only the magnetometer (`MAG_DROPOUT`) is a Warning: it is reported, but the spacecraft keeps flying.

Most faults are fed their samples from outside the flight software (the power monitor's voltage, the command-loss timer). Sensor faults are different: the **sensor monitor** is the detect stage for the onboard sensors, turning each cycle's readings into fault samples. It reads the validity flags the drivers attach to every sample - invalid or missing IMU data raises that source's dropout fault, and an invalid INA228 sample raises `POWER_DROPOUT` - and it also watches the values themselves. A live IMU reading always moves a little, so a stream that goes perfectly flat past a staleness window is treated as frozen and raises the same dropout fault. Staleness is a detector for a dropout, not a separate fault. A sensor that simply is not sampled on a given cycle gets no opinion, so a SIL scenario that never exercises a sensor cannot trip its faults. The monitor only detects; the response still happens in the executive's single fault-response step.

Recovery is deliberately conservative and ground-commanded: a latched fault clears only on a CLEAR_FAULT command, never autonomously, so a flapping sensor cannot toggle a fault on and off. A planned RESET_DEVICE command will let the ground re-initialize a misbehaving peripheral before deciding to clear its fault - both routed through the platform abstraction layer as actions the flight software performs, not inputs it reads.

## Firmware approach

Bare metal first: CMSIS startup, clock, a GPIO heartbeat, a SysTick or timer time base, interrupt-driven UART, then simple packet framing. FreeRTOS comes in when the workload turns genuinely concurrent - multiple rate-critical tasks (sensors, telemetry, commands, a control loop) - not before, when a super-loop is enough, and not as an afterthought.

The firmware is grouped by layer under `bsp/`: `drivers/` (clock, gpio, systick, uart, spi, i2c) - register-level code for the MCU's own peripherals that moves raw bytes - and `devices/` (the ICM-20948 IMU, with the power and temperature sensors as they come up) - the external chips that ride those buses. The frame envelope and message definitions live in `common/protocol/` (C++) - the wire contract the flight software owns and the ground-station firmware will share when it lands. The protocol is the OBC's ground interface, not something the firmware interprets, so the flight software builds and parses the frames while the drivers just carry the bytes. A SysTick time base provides the millisecond clock, and an interrupt-driven multi-instance USART driver spans two UARTs - a console on USART2 (the ST-Link virtual COM port, for the host decoder and debug) and a downlink on USART6 (a header pin, so it can be probed on a scope or logic analyzer and later swapped for a radio). The I2C driver is a polled master; bringing the INA228 up on the bench surfaced a documented F4 erratum - the cell leaves its STOP bit wedged after a transfer (es0298 2.11.3) - so each transfer clears that stale stop with a controller reset before it issues a START.

## Vendored dependencies

Third-party firmware lives under `vendor/` as Git submodules rather than copied-in source.

| Path | What it is |
| ---- | ---------- |
| `vendor/cmsis-core` | CMSIS core headers and support |
| `vendor/cmsis-device-f4` | STM32F4 device headers and startup references |
| `vendor/FreeRTOS-Kernel` | the RTOS kernel, vendored but excluded from the build until the FreeRTOS phase |
| `vendor/etl` | the Embedded Template Library - fixed-capacity, no-heap containers for the flight software |
