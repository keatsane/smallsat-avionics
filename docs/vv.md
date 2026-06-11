# Verification and validation

How this project proves its claims. The short version: requirements are written before the code that satisfies them, every requirement carries a status naming the strongest evidence checked into the repo, and nothing is described as working anywhere the repo can't show it.

## The evidence ladder

A requirement's status in [requirements.md](requirements.md) names the highest level reached, in this order:

planned -> in progress -> unit-verified -> SIL-verified -> bench-verified -> HIL-verified

Each level is a different kind of proof. Unit tests verify a component's logic in isolation (doctest, on the host). SIL verifies behavior end to end through the real executive against declared scenarios, still on the host. Bench and HIL verify the same claims on the physical STM32, with real timing, real links, and eventually real sensors driving real fault paths. A claim only climbs the ladder when the artifact for that level exists - the artifact is listed next to the status.

## The SIL harness

Two pieces, deliberately separated:

- **The shim** (`fsw/sil/sil_shim.cpp`) is the test fixture: a small host executable that links the unmodified flight-software library, reads a per-cycle timeline on stdin, runs `Executive::cycle()` with injected time, and reports everything observable as tagged text lines - mode/fault/command log events as they happen, every telemetry frame hex-encoded, and the final state. It renders no verdicts; an instrument that interprets its own readings stops being trustworthy.
- **The runner** (`tools/sil_runner.py`) is the test conductor: it loads a scenario YAML, compiles the timeline, drives the shim over a pipe, CRC-checks and decodes the telemetry through the same codec the ground tools use (`tools/frames.py`), grades observed against expected, and writes the report. It contains no flight logic and no scenario-specific code (REQ-VV-001).

Time in SIL is injected, not real: a 2-second scenario runs in milliseconds and is exactly reproducible. That is the point of the platform-abstraction layer (REQ-PAL-001/002) - the same flight-software sources run here, in the unit tests, and later on the STM32, with only the platform backend swapped.

What SIL deliberately cannot prove: real-time behavior, link timing, ISR interactions, and anything electrical. Those claims wait for the bench and HIL levels, which is why requirements like the heartbeat emission carry SIL evidence now and still owe HIL evidence later.

## Scenarios and reports

Scenarios live in `fsw/sil/scenarios/` and are cataloged in [scenarios.md](scenarios.md). Each has an id and declares the requirement(s) it verifies; each run writes `docs/reports/<id>.md` recording the scenario id, the requirements, every check's observed-versus-expected result, the injected timeline, and the raw shim output (REQ-VV-002). The checked-in reports are the SIL evidence the requirement statuses point at.

## Traceability

Requirement ids are carried through the whole chain: stated in [requirements.md](requirements.md), referenced in code where a design decision serves a requirement, named in unit-test cases (test suites mirror the requirements sections, one TEST_CASE per requirement id), declared by scenarios, stamped into the flight software's own mode-transition log rows, and recorded in the reports. Walking from a requirement to its proof - or from a log row back to the requirement it serves - never leaves the repo.
