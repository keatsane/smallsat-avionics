# Architecture

The idea is to get flight behavior working in software before asking the STM32 to prove anything with wires attached. Python drives test scenarios into the C++ flight logic, which runs first against a simulated plant (SIL) and later against the real STM32 node over UART (HIL).

```text
Python scenario tools  (fault injection, simulated telemetry, logs, reports)
        |
        v
C++ flight software    (modes, faults, commands, telemetry)
        |
        +----------------------------+
        v                            v
SIL plant model               STM32 HIL node
(power, thermal, sensors)     (bare-metal UART packets first; sensors and watchdog later)
```

## Language split

| Area | Language | What it does |
| ---- | -------- | ------------ |
| STM32 firmware | C | CMSIS register-level startup, drivers, interrupts, timers, UART/SPI/I2C, ADC, GPIO, watchdog, low-level packet I/O |
| Flight software | C++ | mode manager, fault manager, command validation, telemetry model, host unit tests |
| Test and analysis | Python | scenario execution, SIL/HIL orchestration, plotting, log parsing, reports |

The flight logic lives in `fsw/` and is written to be portable. It runs on the host for development and SIL, and the same source cross-compiles onto the STM32 for the integrated build, where it joins the firmware as the on-board computer. It never touches registers directly - it reaches hardware through a platform abstraction layer, backed by the simulator on the host and by the C firmware drivers on the target. So the split is by layer, not by location: the firmware owns the hardware (registers, peripherals, low-level I/O), the flight software owns the decisions (modes, faults, commands), and they meet at that boundary. The flight software allocates nothing dynamically - it uses fixed-capacity containers (ETL) so the same code runs on the host and on the no-heap target.

## Firmware approach

Bare metal first: CMSIS startup, clock, a GPIO heartbeat, a SysTick or timer time base, interrupt-driven UART, then simple packet framing. FreeRTOS comes in when the workload turns genuinely concurrent - multiple rate-critical tasks (sensors, telemetry, commands, a control loop) - not before, when a super-loop is enough, and not as an afterthought.

The firmware is grouped by layer under `bsp/`: `drivers/` (clock, gpio, systick, uart) and `app/` (telemetry). The frame envelope and message definitions live in `common/protocol/` - plain C, compiled by both the firmware and the C++ flight software so the wire format has a single source. Working on the bench today: the SysTick time base, an interrupt-driven multi-instance USART driver, and a CRC-framed telemetry link that streams heartbeat and link-status packets. Telemetry goes out two UARTs: a console on USART2 (the ST-Link virtual COM port, for the host decoder and debug) and a downlink on USART6 (a header pin, so it can be probed on a scope or logic analyzer and later swapped for a radio).

## Vendored dependencies

Third-party firmware lives under `vendor/` as Git submodules rather than copied-in source.

| Path | What it is |
| ---- | ---------- |
| `vendor/cmsis-core` | CMSIS core headers and support |
| `vendor/cmsis-device-f4` | STM32F4 device headers and startup references |
| `vendor/FreeRTOS-Kernel` | the RTOS kernel, vendored but excluded from the build until the FreeRTOS phase |
| `vendor/etl` | the Embedded Template Library - fixed-capacity, no-heap containers for the flight software |
