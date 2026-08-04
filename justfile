# task runner - run `just` with no args to list recipes
set shell := ["bash", "-uc"]
# windows-primary dev: run recipes in powershell so tools on the windows PATH resolve
set windows-shell := ["pwsh.exe", "-NoProfile", "-Command"]

# st-link flasher - version-stamped cubeide path, override with CUBEPROG when it moves
cubeprog := env_var_or_default("CUBEPROG", "C:/ST/STM32CubeIDE_2.1.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.400.202601091506/tools/bin/STM32_Programmer_CLI.exe")

# platformio core - the vs code extension installs it here and does not put it on PATH
pio := env_var_or_default("PIO", env_var("USERPROFILE") / ".platformio/penv/Scripts/pio.exe")

# the nucleo's st-link. without sn= the programmer takes the first probe it finds, and with the
# esc plugged in too that is a coin flip - it will happily write obc firmware onto the g431
# (STM32_Programmer_CLI -l lists probes if this board is ever swapped)
obc_stlink := env_var_or_default("OBC_STLINK", "0670FF363154413043233346")

# native listing grouped by workflow areas
_default:
    @just --list --unsorted --list-heading ""

# format + lint everything (clang-format for c/c++, ruff for python)
[group('formatting')]
format:
    pre-commit run --all-files

# ---------------------------------------------------------------- obc node
# on-board computer - nucleo-f446re, obc/ firmware + fsw/ flight software

# configures every time on purpose - that is what re-globs the source tree, so a newly added file
# is in the image without anyone remembering a step. needs arm-none-eabi-gcc on PATH
[group('obc')]
[doc('build the obc firmware image - firmware plus the flight software cross-compiled in')]
obc-build:
    cmake -S obc -B obc/build-arm -G "Unix Makefiles" && cmake --build obc/build-arm

# build and flash the obc over st-link swd
[group('obc')]
obc-flash: obc-build
    & "{{cubeprog}}" -c port=SWD sn={{obc_stlink}} mode=UR reset=HWrst -d "obc/build-arm/obc.elf" -v -rst

# the port is found by the obc's st-link serial, same as the flasher. at the prompt: `?` lists
# the spacecraft commands, `/?` the console directives that filter the stream live
[group('obc')]
[doc('ground console - telemetry out, commands in; --only/--hide/--all filter, /? at the prompt')]
obc-monitor *flags:
    python tools/uart_monitor.py --stlink {{obc_stlink}} {{flags}}

# kept as the name ci and the docs use. it is the same build as obc-build now that there is only
# one, which is the point
[group('obc')]
[doc('alias for obc-build - the firmware compile+link check ci runs')]
obc-check: obc-build

# ---------------------------------------------------------------- esc node
# reaction wheel - b-g431b-esc1 (stm32g431), simplefoc in torque mode

# build the esc node firmware
[group('esc')]
esc-build:
    & "{{pio}}" run -d esc -e esc

# build and flash the esc over its own on-board st-link (usb)
[group('esc')]
esc-flash:
    & "{{pio}}" run -d esc -e esc -t upload

# the port is found automatically when the esc is the only board plugged in, else pass --port
[group('esc')]
[doc('drive the wheel - volts on the q axis; --watch listens only, --hold holds past the dead-man')]
esc-wheel *args:
    python tools/wheel.py {{args}}

# unplug the obc's 3-wire link first - the esc's uart reaches both it and the usb port
[group('esc')]
[doc('pulse the wheel at rising torque to find where the bearing breaks loose')]
esc-breakaway *args:
    python tools/breakaway.py {{args}}

# ---------------------------------------------------------------- flight software
# portable c++ - builds on the host for unit tests and SIL, and into the obc image

# build the flight software for the host (configure is cached, safe to re-run)
[group('fsw')]
fsw-build:
    cmake -S fsw -B fsw/build -G "Unix Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ && cmake --build fsw/build

# ---------------------------------------------------------------- across the nodes

# nothing here needs a board attached - only flashing does. fsw is not a node: it is the
# portable flight software built for the host, and it is also cross-compiled into the obc image
[group('build')]
[doc('build all three - the host flight software, the obc image, and the esc node')]
build-all: fsw-build obc-build esc-build gsw-build

# ---------------------------------------------------------------- ground station
# feather m0 with the rfm95 on board - receives the beacon and is its own usb serial port

# build the ground station firmware
[group('gsw')]
gsw-build:
    & "{{pio}}" run -d gsw -e gsw

# build and flash the ground station over usb (double-tap reset if the port is not found)
[group('gsw')]
gsw-flash:
    & "{{pio}}" run -d gsw -e gsw -t upload

# the same console the obc uses, pointed at the feather instead of the st-link - and now the same
# in both directions: typed commands go out over lora, telemetry comes back the same way
[group('gsw')]
[doc('ground console over the air - commands out, telemetry back; --all shows per-cycle telemetry')]
gsw-monitor *flags:
    python tools/uart_monitor.py --vid 239A {{flags}}

# ---------------------------------------------------------------- testing

# run every machine-runnable suite - unit + SIL (HIL needs the bench: `just hil`); add "verbose" for detail
[group('testing')]
test detail="": (unit "all" detail) (sil "all" detail)

# unit suites (suite: all, fsw, or tools); add "verbose" for per-test names
[group('testing')]
unit suite="all" detail="":
    {{ if suite == "all" { "just _unit-tools " + detail + " && just _unit-fsw " + detail } else if suite == "fsw" { "just _unit-fsw " + detail } else if suite == "tools" { "just _unit-tools " + detail } else { error("unit suite must be all, fsw, or tools") } }}

_unit-fsw detail="": fsw-build
    {{ if detail == "verbose" { "ctest --test-dir fsw/build --output-on-failure" } else { "./fsw/build/fsw_tests.exe" } }}

_unit-tools detail="":
    pytest {{ if detail == "verbose" { "-v" } else { "-q" } }}

# runs inside `just test` too, as a pytest case - this recipe is for the readable report
[group('testing')]
[doc('check the requirement trace both ways - artifacts exist, cited REQ ids are real')]
trace *flags:
    python tools/traceability.py {{flags}}

# the sim-versus-rig comparison - re-runs the scenario, plots it over a recorded bench run
[group('testing')]
[doc('overlay a recorded bench run on the plant model; --rig/--scenario to pick which')]
overlay *args:
    python tools/overlay.py {{args}}

# ---------------------------------------------------------------- payload

# the survey macro downlinks the frames; this is what turns them into one image
[group('gsw')]
[doc('join survey frames into one wide image, in the order given')]
stitch *frames:
    python tools/stitch.py {{frames}}

# run SIL scenarios (scenario: all, a number, a name, or a path); add "verbose" for every check
[group('testing')]
sil scenario="all" detail="": fsw-build
    python tools/sil_runner.py {{ if detail == "verbose" { "-v" } else { "" } }} {{scenario}}

# run HIL scenarios on the live obc (scenario: all, a number, a name, or a path; needs the bench - never in CI)
[group('testing')]
hil scenario="all":
    python tools/hil_runner.py --stlink {{obc_stlink}} {{scenario}}

# save a scope capture for a HIL test, named and filed (e.g. `just hil-scope 1 frame`)
[group('testing')]
hil-scope test name:
    $n = '{0:000}' -f [int]"{{test}}"; python tools/scope_shot.py "docs/reports/hil/img/HIL-$n-{{name}}.png"

# ---------------------------------------------------------------- bench + setup

# list the serial ports and what is on them - which board is which, before anything is flashed
[group('bench')]
list-ports:
    python -m serial.tools.list_ports -v

# save the scope's screen over usb (siglent sds; needs pyvisa + a visa backend)
[group('bench')]
scope-shot out="scope.png":
    python tools/scope_shot.py {{out}}

# install the python tooling the bench and test suites need
[group('setup')]
deps:
    pip install -r requirements-dev.txt

# install the pre-commit git hook
[group('setup')]
hooks:
    pre-commit install
