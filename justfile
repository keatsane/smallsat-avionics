# task runner - run `just` to list recipes

set shell := ["bash", "-uc"]

# list recipes
default:
    @just --list

# format + lint everything (clang-format for c/c++, ruff for python)
format:
    pre-commit run --all-files

# install the pre-commit hook
hooks:
    pre-commit install
