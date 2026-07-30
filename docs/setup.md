# Setup

## Prerequisites

Everything needed, and what each part is for. Only the first three are needed to run the host test suites; the rest are per-node, so a contributor who only touches flight software can stop after CMake.

| Tool | Needed for | Notes |
| ---- | ---------- | ----- |
| Git | everything | the vendored dependencies are submodules |
| [just](https://github.com/casey/just) | everything | task runner; `just` with no arguments lists every recipe |
| Python 3.12+ | tooling, SIL, HIL | packages declared in `requirements-dev.txt`, installed by `just deps` |
| CMake 3.20+ | flight software, firmware check | drives the host build and the ARM cross-build |
| A host C++17 compiler | flight software | MinGW-w64 g++ on Windows; any recent GCC or Clang elsewhere |
| [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html) | OBC firmware | owns the image that gets flashed, and bundles `arm-none-eabi-gcc` |
| STM32CubeProgrammer CLI | flashing the OBC | ships with CubeIDE; override its path with `CUBEPROG` |
| `arm-none-eabi-gcc` on PATH | `just obc-check` | CubeIDE bundles one but does not add it to PATH |
| [PlatformIO Core](https://platformio.org/) | ESC firmware | the VS Code extension installs it; override its path with `PIO` |
| A VISA backend | scope captures only | NI-VISA or equivalent, under `pyvisa`; nothing else needs it |

Three environment variables exist because their defaults are machine-specific: `CUBEPROG` and `PIO` are version-stamped install paths, and `OBC_STLINK` pins the Nucleo's ST-Link serial so an unpinned flash cannot land on the ESC. All three have working defaults in the justfile for this bench.

## Get the code

Clone the repo, then pull in the vendored dependencies - CMSIS, FreeRTOS, and ETL live under `vendor/` as Git submodules:

```bash
git submodule update --init --recursive
```

The Python tooling - the ground console, the SIL and HIL runners, the test suites - has its dependencies declared in `requirements-dev.txt`:

```bash
just deps
```

CI installs from that same file, so a dependency that is not declared there fails the build rather than working only on the machine it was added on.

## Firmware

Two boards run firmware, and each has its own recipes - `just` with no arguments lists them grouped by node.

**On-board computer** (Nucleo-F446RE, `obc/` + `fsw/`) builds with CMake and the `arm-none-eabi` toolchain, and flashes over the onboard ST-Link. CubeIDE can still open and build the project, but it is kept as a debugger rather than as the authority on the image - see the note under Tests. The board's UART comes back over the same USB cable as a virtual COM port.

```bash
just obc-build           # build the image
just obc-flash           # build, then flash over st-link swd
```

**Reaction-wheel node** (B-G431B-ESC1, `esc/`) is a PlatformIO project running SimpleFOC, flashed over its own onboard ST-Link.

```bash
just esc-build           # build the image
just esc-flash           # build, then upload over usb
```

### Camera register tables

The OV2640's JPEG bring-up is a few hundred SCCB register writes whose values the datasheet does not explain, so like every other driver in the wild the tables come from OmniVision by way of ArduCAM's library rather than being retyped. They live in `vendor/arducam/ov2640_regs.h`, byte-exact from upstream so it can be re-fetched and diffed:

```bash
curl -o vendor/arducam/ov2640_regs.h https://raw.githubusercontent.com/ArduCAM/Arduino/master/ArduCAM/ov2640_regs.h
```

Two small pieces bridge it into this tree, and neither touches the vendored file. `vendor/arducam/ArduCAM.h` is a local stand-in supplying the `PROGMEM` macro and the `sensor_reg` struct that upstream expects from the Arduino environment. `obc/Src/devices/ov2640_regs.cpp` maps upstream's table names onto the driver's, and is C++ rather than C because upstream is - some of its tables use brace initialization with no `=`, which is legal C++ and not legal C.

## Monitoring telemetry

The OBC streams CRC-framed telemetry over its virtual COM port. To decode it on the host:

```bash
just obc-monitor                      # decode telemetry frames
```

The port is found by the OBC's ST-Link serial number - the same one that pins the flasher - so neither has to be told which board it is talking to. Both nodes enumerate as ST-Link virtual COM ports, so with the whole stack plugged in there is otherwise no way to tell them apart, and monitoring the wrong board is the same class of mistake as flashing the wrong one. `just list-ports` shows what is attached.

The wheel node speaks the same frame format on its own link, so the bench driver both commands it and prints what comes back:

```bash
just esc-wheel 2.0 --hold             # 2.0 V on the q axis, held
just esc-wheel --watch                # listen only
```

## Commanding

The OBC accepts framed commands on the same links it sends telemetry out on - the ST-Link virtual COM port on the bench, and the PC6/PC7 header the radio lands on later. A serial port takes one program at a time, so commanding lives in the monitor rather than in a second tool competing for the link: type at the prompt while telemetry streams past.

```bash
just obc-monitor
```

Then `SET_MODE DETUMBLE`, `CAPTURE_IMAGE`, `CLEAR_FAULT WHEEL_DROPOUT`, or `?` to list what is accepted. Arguments are names rather than ids, resolved against the same catalogs the firmware is generated from, so a typo is refused on the ground instead of arriving as a valid-looking wrong id. Because the command goes out on the link the telemetry comes back on, the command, its ack, and the telemetry showing its effect all land in one stream in order. Tab completes command and argument names, and up-arrow recalls what was sent; telemetry scrolls above the prompt rather than over what is half typed.

`--keepalive` sends a NOOP every two seconds, which is what holds off `COMMAND_LINK_LOSS` without inhibiting it. `--read-only` disables transmit entirely.

Scripting uses the same syntax without the prompt - pipe commands in: `echo SET_MODE STANDBY | python tools/uart_monitor.py`.

## SIL scenarios

The SIL harness runs declared fault-injection scenarios against the flight software on the host - no hardware involved. See [verification.md](verification.md) for the harness, the evidence ladder, and the scenario catalog.

```bash
just sil                              # run the whole scenario suite, reports to docs/reports/sil/
just sil 1                            # or one scenario, by number or name
```

## Formatting

Formatting and linting run through pre-commit - clang-format for C/C++, ruff for Python - and the same checks run in CI, which also runs clang-tidy static analysis on the flight software. With Python and pre-commit installed:

```bash
pre-commit install   # once; the hooks then run on every commit
just format          # format and lint everything on demand
```

## Tests

Tests come in levels: unit suites (pytest for the Python tooling, CMake + doctest for the C++ flight software), the SIL scenario suite, and the HIL scenarios on the bench. `just test` runs everything machine-runnable, and the same suites run in CI; HIL needs the board, so it stays manual.

```bash
just test                # everything machine-runnable: all unit suites + the SIL suite
just unit                # both unit suites (or one: `just unit fsw`, `just unit tools`)
just sil                 # the SIL scenario suite (or one scenario: `just sil 5`)
just hil                 # the HIL campaign on the live board (or one: `just hil 2`)
just obc-check           # cross-compile and link the firmware, the same check CI runs
```

`obc-check` and `obc-build` are the same build - one CMake cross-build that produces the image `just obc-flash` writes to the board and the check CI runs. They were separate once, with CubeIDE's generated makefiles owning the flashable image, and that cost bench time twice: those makefiles list each source file explicitly and only the IDE regenerates them, so adding a source file broke the build with undefined references until someone refreshed and cleaned inside CubeIDE. CubeIDE remains useful as a debugger and can still build the project on its own, but it is no longer the authority. The build links rather than only compiling, because a symbol that compiles and never reaches the linker is a failure this project has hit twice.

Every test recipe takes an optional trailing `verbose` argument (`just test verbose`, `just unit fsw verbose`, ...) that switches to per-test names - through ctest for the C++ suite, `pytest -v` for the tooling, and per-check output for SIL.

`just unit fsw` does the configure, build, and run in one step.

## Working environment

Everything is built and run on Windows: firmware in STM32CubeIDE, git and tooling from PowerShell. Python handles the test and sim tooling as those parts come online.
