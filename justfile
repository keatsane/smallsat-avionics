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

# build the flight software and/or firmware (target: all, fsw, or bsp)
[group('build')]
build target="all":
    {{ if target == "all" { "just _build-fsw && just _build-bsp" } else if target == "fsw" { "just _build-fsw" } else if target == "bsp" { "just _build-bsp" } else { error("build target must be all, fsw, or bsp") } }}

# configure is cached, safe to re-run
_build-fsw:
    cmake -S fsw -B fsw/build -G "Unix Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ && cmake --build fsw/build

# bsp/Debug makefiles come from a first cubeide build; "all" is load-bearing -
# the generated subdir.mk clean targets get included before the all target, so
# bare make's default goal is clean (cubeide itself always invokes make all)
_build-bsp:
    make -C bsp/Debug -j all

# build and flash the firmware image over st-link swd
[group('build')]
flash: (build "bsp")
    & "{{cubeprog}}" -c port=SWD mode=UR reset=HWrst -d "bsp/Debug/bsp.elf" -v -rst

# run every machine-runnable suite - unit + SIL (HIL needs the bench: `just hil`); add "verbose" for detail
[group('testing')]
test detail="": (unit "all" detail) (sil "all" detail)

# unit suites (suite: all, fsw, or tools); add "verbose" for per-test names
[group('testing')]
unit suite="all" detail="":
    {{ if suite == "all" { "just _unit-tools " + detail + " && just _unit-fsw " + detail } else if suite == "fsw" { "just _unit-fsw " + detail } else if suite == "tools" { "just _unit-tools " + detail } else { error("unit suite must be all, fsw, or tools") } }}

_unit-fsw detail="": (build "fsw")
    {{ if detail == "verbose" { "ctest --test-dir fsw/build --output-on-failure" } else { "./fsw/build/fsw_tests.exe" } }}

_unit-tools detail="":
    pytest {{ if detail == "verbose" { "-v" } else { "-q" } }}

# run SIL scenarios (scenario: all, a number, a name, or a path); add "verbose" for every check
[group('testing')]
sil scenario="all" detail="": (build "fsw")
    python tools/sil_runner.py {{ if detail == "verbose" { "-v" } else { "" } }} {{scenario}}

# run HIL scenarios on the live board (scenario: all, a number, a name, or a path; needs the bench - never in CI)
[group('testing')]
hil port scenario="all":
    python tools/hil_runner.py {{port}} {{scenario}}

# save a scope capture for a HIL test, named and filed (e.g. `just hil-scope 1 frame`)
[group('testing')]
hil-scope test name:
    $n = '{0:000}' -f [int]"{{test}}"; python tools/scope_shot.py "docs/reports/hil/img/HIL-$n-{{name}}.png"

# watch decoded UART frames from a flashed board (python -m serial.tools.list_ports to see ports).
# pass flags after the port: --raw (echo bytes), --only HEARTBEAT,POWER_DATA, --hide IMU_DATA
[group('bench')]
uart port *flags:
    python tools/uart_monitor.py {{flags}} {{port}}

# save the scope's screen over usb (siglent sds; needs pyvisa + a visa backend)
[group('bench')]
scope-shot out="scope.png":
    python tools/scope_shot.py {{out}}

# install the pre-commit git hook
[group('setup')]
hooks:
    pre-commit install
