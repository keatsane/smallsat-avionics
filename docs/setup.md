# Setup

## Get the code

Clone the repo, then pull in the vendored dependencies - CMSIS, FreeRTOS, and ETL live under `vendor/` as Git submodules:

```bash
git submodule update --init --recursive
```

## Firmware

The STM32 firmware is an STM32CubeIDE project under `bsp/`. Open it in CubeIDE, build, and flash to the Nucleo-F446RE over its onboard ST-Link. The board's UART comes back over the same USB cable as a virtual COM port.

## Monitoring telemetry

The node streams CRC-framed telemetry over that virtual COM port. To decode it on the host:

```bash
pip install pyserial
python -m serial.tools.list_ports     # find the COM port
python tools/uart_monitor.py COM3     # decode heartbeat and link-status frames
```

## SIL scenarios

The SIL harness runs declared fault-injection scenarios against the flight software on the host - no hardware involved. See [scenarios.md](scenarios.md) for the catalog and [vv.md](vv.md) for how the harness fits the verification approach.

```bash
pip install pyyaml
just sil                              # run the whole scenario suite, reports to docs/reports/sil/
just sil fsw/sil/scenarios/sil_001_undervoltage.yaml    # or one scenario
```

## Formatting

Formatting and linting run through pre-commit - clang-format for C/C++, ruff for Python - and the same checks run in CI, which also runs clang-tidy static analysis on the flight software. With Python and pre-commit installed:

```bash
pre-commit install   # once; the hooks then run on every commit
just format          # format and lint everything on demand
```

## Tests

Tests come in levels: unit suites (pytest for the Python tooling, CMake + doctest for the C++ flight software) and the SIL scenario suite, with HIL joining later. `just test` runs everything, and the same suites run in CI.

```bash
just test                # everything: all unit suites + the SIL scenario suite
just unit                # both unit suites
just unit-fsw            # c++ flight software only
just unit-tools          # python tooling only (config in pyproject.toml)
just sil                 # the SIL scenario suite
```

Every test recipe takes an optional `verbose` argument (`just test verbose`, `just unit-fsw verbose`, ...) that switches to per-test names - through ctest for the C++ suite, `pytest -v` for the tooling, and per-check output for SIL.

The C++ suite needs CMake and a host C++ compiler; `just unit-fsw` does the configure, build, and run in one step.

## Working environment

Everything is built and run on Windows: firmware in STM32CubeIDE, git and tooling from PowerShell. Python handles the test and sim tooling as those parts come online.
