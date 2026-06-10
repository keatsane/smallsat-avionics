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

# run all host test suites (python + c++)
test: test-py test-cpp

# run the python host tests
test-py:
    pytest -q

# build + run the flight-software unit tests (clean summary; full detail only on failure)
test-cpp: build-cpp
    ./fsw/build/fsw_tests.exe

# build + run the unit tests through ctest (per-test names, for CI)
test-cpp-ci: build-cpp
    ctest --test-dir fsw/build --output-on-failure

# build the c++ flight software (configure is cached, safe to re-run)
build-cpp:
    cmake -S fsw -B fsw/build -G "Unix Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ && cmake --build fsw/build

# install the pre-commit git hook
hooks:
    pre-commit install
