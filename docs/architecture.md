# Architecture

The idea is to get flight behavior working in software before asking the STM32 to prove anything with wires attached. Python drives declared test scenarios into the C++ flight logic, first on the host (SIL) and later against the real STM32 node over UART (HIL).

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

On the SIL side this exists today: scenarios in `fsw/sil/scenarios/` drive the unmodified flight-software library through a small shim executable, and the runner grades the observed behavior and writes reports to `docs/reports/` (the details live in [vv.md](vv.md)). A physics plant model joins when real sensor and attitude dynamics matter - NASA's 42 simulator in the ADCS phase.

## Language split

| Area | Language | What it does |
| ---- | -------- | ------------ |
| STM32 firmware | C | CMSIS register-level startup, drivers, interrupts, timers, UART/SPI/I2C, ADC, GPIO, watchdog, low-level packet I/O |
| Flight software | C++ | mode manager, fault manager, command validation, telemetry model, host unit tests |
| Test and analysis | Python | scenario execution, SIL/HIL orchestration, plotting, log parsing, reports |

The flight logic lives in `fsw/` and is written to be portable. It runs on the host for development and SIL, and the same source cross-compiles onto the STM32 for the integrated build, where it joins the firmware as the on-board computer. It never touches registers directly - it reaches hardware through a platform abstraction layer, backed by the simulator on the host and by the C firmware drivers on the target. So the split is by layer, not by location: the firmware owns the hardware (registers, peripherals, low-level I/O), the flight software owns the decisions (modes, faults, commands), and they meet at that boundary. The flight software allocates nothing dynamically - it uses fixed-capacity containers (ETL) so the same code runs on the host and on the no-heap target.

## Communication links

Everything between the satellite and the ground rides framed, CRC-checked packets (`common/protocol/`), but it is not one undifferentiated "telemetry" stream - there are two logical links with different jobs.

```text
   SATELLITE (OBC)                                   GROUND STATION (host PC / Teensy box)

   command link - TT&C, low-rate, always on (UART / LoRa RFM95)
        commands   <---------------------------------   SET_MODE, CLEAR_FAULT, NOOP, ...                (uplink)
        telemetry  --------------------------------->   heartbeat (mode + fault bitmask), link status, acks  (downlink)

   payload link - high-rate, only in DOWNLINK mode (nRF24 2.4 GHz)
        payload    --------------------------------->   bulk imaging data, chunked over a contact pass  (downlink)
```

The **command link** is the always-on TT&C channel: commands up, housekeeping telemetry down. It is low-rate and stays on so the ground can always reach the spacecraft. The **payload link** is separate and high-rate; it powers up only in DOWNLINK mode to empty the imaging buffer over a contact pass, because a power-hungry high-rate radio cannot be always on. Both carry the same frame format - only the transport changes - so the codec is written once.

One distinction the earlier docs blurred: "telemetry" and "downlink" are not the same thing. Telemetry is the continuous housekeeping beacon (mode, faults, health) on the command link; "downlink" is the payload-emptying *mode* on the payload link. Both travel satellite-to-ground, but they are different content on different links.

### What the satellite can observe

The spacecraft only ever sees its own end of a link - it cannot hear its own transmissions, so it can never directly know whether the ground received a downlink. That asymmetry sets which link faults exist at all:

- Uplink silence is observable. If no valid command arrives within a timeout, a command-loss timer raises `COMMAND_LINK_LOSS` - the one onboard link-health fault, standing for "we have lost the ground," detected purely from the command side going quiet.
- Downlink delivery is not observable onboard. Whether the ground is hearing the telemetry is detected on the ground, from gaps in the heartbeat sequence number - never by a fault on the satellite.
- Sequence numbers (`command_t.seq`, `heartbeat_t.seq`) detect drops, not loss - the receiving end spotting a missed or duplicate packet, a finer signal than the link being down.
- `uart_status_t` reports link quality (UART receive-line error counts: overrun, framing, noise, dropped) as diagnostic telemetry, not as a fault. Each transport gets its own status message as it lands (LoRa: RSSI/SNR, nRF24: retransmit counts), since the quality observables are transport-specific.
- The future payload link is the exception: the nRF24 radio auto-acknowledges, so the satellite can tell a payload packet was not received and raise a `PAYLOAD_LINK_LOSS` - but only once that hardware exists.

### Beacon rates and the loss timeout

Both directions beacon at 1 Hz: the satellite sends a heartbeat every second, and the ground sends a NOOP keep-alive every second (that is what NOOP is for - the command-loss timer only works because the ground promises not to be silent). Either end declares link loss after 5 seconds of silence - five missed beacons, so a few dropped packets never false-alarm. These are bench rates, chosen so a human watching the console sees a live system; a real spacecraft beacons far slower and tolerates hours of command silence before safing, but the architecture is the same with different constants.

## Firmware approach

Bare metal first: CMSIS startup, clock, a GPIO heartbeat, a SysTick or timer time base, interrupt-driven UART, then simple packet framing. FreeRTOS comes in when the workload turns genuinely concurrent - multiple rate-critical tasks (sensors, telemetry, commands, a control loop) - not before, when a super-loop is enough, and not as an afterthought.

The firmware is grouped by layer under `bsp/`: `drivers/` (clock, gpio, systick, uart) - register-level code that owns the hardware and moves raw bytes. The frame envelope and message definitions live in `common/protocol/` (C++) - the wire contract the flight software owns and the ground-station firmware will share when it lands. The protocol is the OBC's ground interface, not something the firmware interprets, so the flight software builds and parses the frames while the drivers just carry the bytes. Working on the bench today: the SysTick time base and an interrupt-driven multi-instance USART driver across two UARTs - a console on USART2 (the ST-Link virtual COM port, for the host decoder and debug) and a downlink on USART6 (a header pin, so it can be probed on a scope or logic analyzer and later swapped for a radio).

## Vendored dependencies

Third-party firmware lives under `vendor/` as Git submodules rather than copied-in source.

| Path | What it is |
| ---- | ---------- |
| `vendor/cmsis-core` | CMSIS core headers and support |
| `vendor/cmsis-device-f4` | STM32F4 device headers and startup references |
| `vendor/FreeRTOS-Kernel` | the RTOS kernel, vendored but excluded from the build until the FreeRTOS phase |
| `vendor/etl` | the Embedded Template Library - fixed-capacity, no-heap containers for the flight software |
