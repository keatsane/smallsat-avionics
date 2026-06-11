# task runner - run `just` with no args to list recipes
set shell := ["bash", "-uc"]
# windows-primary dev: run recipes in powershell so tools on the windows PATH resolve
set windows-shell := ["pwsh.exe", "-NoProfile", "-Command"]

# st-link flasher - version-stamped cubeide path, override with CUBEPROG when it moves
cubeprog := env_var_or_default("CUBEPROG", "C:/ST/STM32CubeIDE_2.1.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.400.202601091506/tools/bin/STM32_Programmer_CLI.exe")

# native listing grouped by workflow areas
_default:
    @just --list --unsorted --list-heading ""

# format + lint everything (clang-format for c/c++, ruff for python)
[group('formatting')]
format:
    pre-commit run --all-files

# build both host fsw and cubeide debug firmware
[group('build')]
build: build-fsw build-bsp
    @Write-Output "build complete"

# build the c++ flight software (configure is cached, safe to re-run)
[group('build')]
build-fsw:
    cmake -S fsw -B fsw/build -G "Unix Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ && cmake --build fsw/build

# build the cubeide debug firmware image (bsp/Debug makefiles come from a first ide build)
[group('build')]
build-bsp:
    make -C bsp/Debug -j

# build and flash the debug firmware image over st-link swd
[group('build')]
flash: build-bsp
    & "{{cubeprog}}" -c port=SWD mode=UR reset=HWrst -d "bsp/Debug/bsp.elf" -v -rst

# flash an already-built firmware image over st-link swd
[group('build')]
flash-only elf="bsp/Debug/bsp.elf":
    & "{{cubeprog}}" -c port=SWD mode=UR reset=HWrst -d "{{elf}}" -v -rst

# run every machine-runnable suite - unit + SIL (HIL needs the bench: `just hil`); add "verbose" for detail
[group('testing')]
test detail="": (unit detail) (sil "fsw/sil/scenarios" detail)

# run all unit suites (python tooling + c++ flight software); add "verbose" for per-test names
[group('testing')]
unit detail="": (unit-tools detail) (unit-fsw detail)

# c++ flight-software unit tests; add "verbose" for per-test names via ctest
[group('testing')]
unit-fsw detail="": build-fsw
    {{ if detail == "verbose" { "ctest --test-dir fsw/build --output-on-failure" } else { "./fsw/build/fsw_tests.exe" } }}

# python tooling unit tests (frames codec + sil runner); add "verbose" for per-test names
[group('testing')]
unit-tools detail="":
    pytest {{ if detail == "verbose" { "-v" } else { "-q" } }}

# run SIL scenarios (the whole suite, or pass one yaml); add "verbose" for every check
[group('testing')]
sil scenario="fsw/sil/scenarios" detail="": build-fsw
    python tools/sil_runner.py {{ if detail == "verbose" { "-v" } else { "" } }} {{scenario}}

# run the bare SIL shim on one scenario's timeline, ungraded (debugging aid)
[group('testing')]
sil-shim scenario="fsw/sil/scenarios/sil_001_undervoltage.yaml": build-fsw
    python tools/sil_runner.py --compile-only {{scenario}} | ./fsw/build/sil_shim.exe

# run a HIL scenario against the live board (needs the bench, so manual - never in CI)
[group('testing')]
hil port scenario="fsw/hil/scenarios/hil_001_timing.yaml":
    python tools/hil_runner.py {{scenario}} {{port}}

# save a scope capture for a HIL test, named and filed (e.g. `just hil-scope 1 frame`)
[group('testing')]
hil-scope test name:
    $n = '{0:000}' -f [int]"{{test}}"; python tools/scope_shot.py "docs/reports/hil/img/HIL-$n-{{name}}.png"

# watch decoded UART frames from a flashed board (python -m serial.tools.list_ports to see ports)
[group('bench')]
uart port:
    python tools/uart_monitor.py {{port}}

# save the scope's screen over usb (siglent sds; needs pyvisa + a visa backend)
[group('bench')]
scope-shot out="scope.png":
    python tools/scope_shot.py {{out}}

# install the pre-commit git hook
[group('setup')]
hooks:
    pre-commit install
