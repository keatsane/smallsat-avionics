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

The flight logic lives in `flight_sw/` and is written to be portable. It runs on the host for development and SIL, and the same source cross-compiles onto the STM32 for the integrated build, where it joins the firmware as the on-board computer. It never touches registers directly - it reaches hardware through a platform abstraction layer, backed by the simulator on the host and by the C firmware drivers on the target. So the split is by layer, not by location: the firmware owns the hardware (registers, peripherals, low-level I/O), the flight software owns the decisions (modes, faults, commands), and they meet at that boundary.

## Firmware approach

Bare metal first: CMSIS startup, clock, a GPIO heartbeat, a SysTick or timer time base, interrupt-driven UART, then simple packet framing. FreeRTOS comes in later, once the bare-metal packet I/O is solid on the bench, so it isn't covering for unfinished bring-up.

The firmware is grouped by layer under `firmware/avionics_node_f446/`: `drivers/` (clock, gpio, systick, uart), `protocol/` (the frame envelope and message definitions), and `app/` (telemetry). Working on the bench today: the SysTick time base, the interrupt-driven USART2 driver, and a CRC-framed telemetry link that streams heartbeat and link-status packets to a host decoder.

## Vendored dependencies

Third-party firmware lives under `firmware/vendor/` as Git submodules rather than copied-in source.

| Path | What it is |
| ---- | ---------- |
| `firmware/vendor/cmsis-core` | CMSIS core headers and support |
| `firmware/vendor/cmsis-device-f4` | STM32F4 device headers and startup references |
| `firmware/vendor/FreeRTOS-Kernel` | the RTOS kernel, vendored ahead of the FreeRTOS work |
