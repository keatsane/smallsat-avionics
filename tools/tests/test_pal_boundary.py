"""REQ-PAL-001 as a check instead of a promise.

The requirement says the flight software reaches platform services only through the PAL and
contains no register or peripheral access. Its verification method is inspection, which stops
being done the moment someone is in a hurry - the same failure the REQ-VV-004 trace checker was
written to end. A peripheral header is the thing you cannot touch hardware without, so the
include list is the boundary, and it is mechanically checkable.

What this would have caught: a `#include "stm32f4xx.h"` or a `#include "drivers/uart.h"` inside
fsw/, which compiles perfectly well in the cross build and silently makes the flight software
un-runnable on the host - breaking REQ-PAL-002's "identical source on both targets" as a side
effect, and not failing until someone tries to run the unit tests.

The rules are applied to text rather than to paths so the negative cases below can prove each one
actually fires. A boundary check that has only ever seen a compliant tree is a promise again.
"""

import re
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]

# the portable flight software - the code that must build for host and target unchanged
FSW_DIRS = ("fsw/src", "fsw/include")

# an include is allowed if it is the standard library, ETL, the fsw's own headers, or the shared
# wire protocol. anything else is either a platform detail or a dependency nobody decided on
ALLOWED_PREFIXES = ("fsw/", "protocol/", "etl/")

INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.M)
# the trailing \w* is load-bearing and was missing at first: `\bNVIC\b` never matches
# NVIC_EnableIRQ, because '_' is a word character and so no boundary exists there. that is how
# these names are almost always written (RCC_APB1ENR, SysTick_Config), so without it the check
# passed everything it most needed to catch. the negative cases below are what found it
BANNED = re.compile(r"\b(RCC|GPIO[A-K]|USART[0-9]|SPI[0-9]|I2C[0-9]|NVIC|SysTick)\w*")


def strip_comments(text: str) -> str:
    """Comments explain hardware; code must not touch it. Only the code is checked."""
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)


def platform_includes(text: str) -> list:
    """Includes that reach outside the flight software's allowed set."""
    bad = []
    for inc in INCLUDE.findall(text):
        if "/" not in inc and "." not in inc:
            continue  # <cstdint>, <optional> - the standard library is extensionless
        if not inc.startswith(ALLOWED_PREFIXES):
            bad.append(inc)
    return bad


def register_hits(text: str) -> list:
    """Peripheral names appearing in code rather than in a comment."""
    return BANNED.findall(strip_comments(text))


def fsw_sources() -> list:
    files = []
    for d in FSW_DIRS:
        files.extend(p for p in (REPO_ROOT / d).rglob("*") if p.suffix in (".cpp", ".hpp", ".h"))
    return sorted(files)


def test_fsw_sources_exist():
    # a boundary check that silently checks nothing is worse than no check at all
    assert len(fsw_sources()) >= 10


@pytest.mark.parametrize("path", fsw_sources(), ids=lambda p: p.name)
def test_fsw_file_stays_inside_the_pal(path: Path):
    text = path.read_text(encoding="utf-8")
    rel = path.relative_to(REPO_ROOT)
    assert not platform_includes(text), (
        f"{rel} includes a header outside the PAL. Flight software may only include the standard "
        f"library, etl/, fsw/ and protocol/ (REQ-PAL-001)."
    )
    assert not register_hits(text), (
        f"{rel} references a peripheral in code. Flight software describes decisions, not "
        f"hardware (REQ-PAL-001)."
    )


# --- the rules actually fire ---


@pytest.mark.parametrize(
    "inc",
    [
        '#include "stm32f4xx.h"',  # the vendor header
        '#include "drivers/uart.h"',  # an obc driver
        "#include <stm32f446xx.h>",
        '#include "devices/ov2640.h"',
    ],
)
def test_platform_include_is_rejected(inc):
    assert platform_includes(inc) == [inc.split('"')[1] if '"' in inc else "stm32f446xx.h"]


@pytest.mark.parametrize(
    "inc",
    [
        "#include <cstdint>",
        "#include <optional>",
        '#include "fsw/executive.hpp"',
        '#include "protocol/msg.hpp"',
        '#include "etl/vector.h"',
    ],
)
def test_allowed_include_passes(inc):
    assert platform_includes(inc) == []


def test_register_access_is_rejected():
    assert register_hits("void f() { RCC->CR |= 1; }") == ["RCC"]
    assert register_hits("GPIOA->ODR = 0;") == ["GPIOA"]
    # the underscored forms are the common ones, and the first version of BANNED missed them all
    assert register_hits("NVIC_EnableIRQ(x);") == ["NVIC"]
    assert register_hits("RCC_APB1ENR |= bit;") == ["RCC"]
    assert register_hits("SysTick_Config(n);") == ["SysTick"]
    assert register_hits("USART2->DR = c;") == ["USART2"]


def test_hardware_named_in_a_comment_is_fine():
    # the flight software is allowed to explain what the platform does for it
    assert register_hits("// the platform drives USART2 for us\nint x = 0;") == []
    assert register_hits("/* RCC is the platform's problem */\nint y = 1;") == []
