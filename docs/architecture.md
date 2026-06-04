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

The flight logic lives in `flight_sw/` and runs on the host; the firmware only owns what actually needs the MCU.

## Firmware approach

Bare metal first: CMSIS startup, clock, a GPIO heartbeat, a SysTick or timer time base, interrupt-driven UART, then simple packet framing. FreeRTOS comes in later, once the bare-metal packet I/O is solid on the bench, so it isn't covering for unfinished bring-up.

## Vendored dependencies

Third-party firmware lives under `firmware/vendor/` as Git submodules rather than copied-in source.

| Path | What it is |
| ---- | ---------- |
| `firmware/vendor/cmsis-core` | CMSIS core headers and support |
| `firmware/vendor/cmsis-device-f4` | STM32F4 device headers and startup references |
| `firmware/vendor/FreeRTOS-Kernel` | the RTOS kernel, vendored ahead of the FreeRTOS work |
