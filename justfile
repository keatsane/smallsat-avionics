# task runner - run `just` with no args to list recipes

set shell := ["bash", "-uc"]
# windows-primary dev: run recipes in powershell so tools on the windows PATH resolve
set windows-shell := ["pwsh.exe", "-NoProfile", "-Command"]

# list the recipes (this is what bare `just` runs)
default:
    @just --list --unsorted

# format + lint everything (clang-format for c/c++, ruff for python)
format:
    pre-commit run --all-files

# run every test suite in the repo - unit + SIL (HIL joins at phase 4); add "verbose" for detail
test detail="": (unit detail) (sil "fsw/sil/scenarios" detail)

# run all unit suites (python tooling + c++ flight software); add "verbose" for per-test names
unit detail="": (unit-tools detail) (unit-fsw detail)

# c++ flight-software unit tests; add "verbose" for per-test names via ctest
unit-fsw detail="": build-fsw
    {{ if detail == "verbose" { "ctest --test-dir fsw/build --output-on-failure" } else { "./fsw/build/fsw_tests.exe" } }}

# python tooling unit tests (frames codec + sil runner); add "verbose" for per-test names
unit-tools detail="":
    pytest {{ if detail == "verbose" { "-v" } else { "-q" } }}

# build the c++ flight software (configure is cached, safe to re-run)
build-fsw:
    cmake -S fsw -B fsw/build -G "Unix Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ && cmake --build fsw/build

# run SIL scenarios (the whole suite, or pass one yaml); add "verbose" for every check
sil scenario="fsw/sil/scenarios" detail="": build-fsw
    python tools/sil_runner.py {{ if detail == "verbose" { "-v" } else { "" } }} {{scenario}}

# run the bare SIL shim on one scenario's timeline, ungraded (debugging aid)
sil-shim scenario="fsw/sil/scenarios/sil_001_undervoltage.yaml": build-fsw
    python tools/sil_runner.py --compile-only {{scenario}} | ./fsw/build/sil_shim.exe

# install the pre-commit git hook
hooks:
    pre-commit install
