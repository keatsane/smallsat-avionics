# Setup

## Get the code

Clone the repo, then pull in the vendored firmware - CMSIS and FreeRTOS live under `firmware/vendor/` as Git submodules:

```bash
git submodule update --init --recursive
```

## Firmware

The STM32 firmware is an STM32CubeIDE project under `firmware/avionics_node_f446/`. Open it in CubeIDE, build, and flash to the Nucleo-F446RE over its onboard ST-Link. The board's UART comes back over the same USB cable as a virtual COM port.

## Formatting

Formatting and linting run through pre-commit - clang-format for C/C++, ruff for Python - and the same checks run in CI. With Python and pre-commit installed:

```bash
pre-commit install   # once; the hooks then run on every commit
just format          # format and lint everything on demand
```

## Working environment

Everything is built and run on Windows: firmware in STM32CubeIDE, git and tooling from PowerShell. Python handles the test and sim tooling as those parts come online.
