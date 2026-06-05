# task runner - run `just` to list recipes

set shell := ["bash", "-uc"]
# windows-primary dev: run recipes in powershell so tools on the windows PATH (pre-commit) resolve
set windows-shell := ["pwsh.exe", "-NoProfile", "-Command"]

# list recipes
default:
    @just --list

# format + lint everything (clang-format for c/c++, ruff for python)
format:
    pre-commit run --all-files

# run the host tests
test:
    pytest

# install the pre-commit hook
hooks:
    pre-commit install
