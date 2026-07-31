# Verification and validation

How claims stay tied to evidence. Requirements are written before the code, each carries a status naming the strongest evidence in the repo, and nothing is described as working somewhere the repo cannot show it.

## The evidence ladder

A status in [requirements.md](requirements.md) names the highest level reached:

planned -> in progress -> unit-verified -> SIL-verified -> bench-verified -> HIL-verified

Unit tests check logic in isolation (doctest, host). SIL runs the real executive end to end against declared scenarios, still on the host. Bench and HIL run the same claims on the STM32 with real timing and real links. A claim climbs only when the artifact for that level exists, and the status names it.

## Traceability

Requirement ids run through the whole chain: stated in [requirements.md](requirements.md), cited in code, named in unit-test cases, declared by scenarios, stamped into mode-transition log rows, and recorded in reports. `just trace` checks the chain.

## SIL

Two pieces, kept separate:

- **The shim** (`fsw/sil/sil_shim.cpp`) links the unmodified flight-software library, reads a per-cycle timeline on stdin, runs `Executive::cycle()` with injected time, and prints everything observable as tagged lines. It grades nothing.
- **The runner** (`tools/sil_runner.py`) compiles the scenario YAML, drives the shim, decodes telemetry through the same codec the ground tools use, grades observed against expected, and writes the report. No flight logic, no scenario-specific code (REQ-VV-001).

Time is injected, so a 2-second scenario runs in milliseconds and is exactly reproducible. What SIL cannot cover: real-time behavior, link timing, ISR interactions, anything electrical.

```
just sil                # the whole suite
just sil 5              # by number...
just sil command_loss   # ...or by name fragment
```

Exit 0 all passed, 1 a scenario failed, 2 harness error. Reports are written either way.

### Catalog

| ID | Title | Verifies | Report |
| -- | ----- | -------- | ------ |
| SIL-001 | undervoltage drives safe mode | REQ-FAULT-002 | [SIL-001.md](reports/sil/SIL-001.md) |
| SIL-002 | illegal-in-mode command is rejected and acked | REQ-CMD-001, REQ-CMD-003 | [SIL-002.md](reports/sil/SIL-002.md) |
| SIL-003 | degraded fault latches without entering safe mode | REQ-FAULT-005 | [SIL-003.md](reports/sil/SIL-003.md) |
| SIL-004 | transient bad samples are debounced away | REQ-FAULT-004, REQ-FAULT-009 | [SIL-004.md](reports/sil/SIL-004.md) |
| SIL-005 | command-link silence trips the dead man into safe mode | REQ-CMD-002 | [SIL-005.md](reports/sil/SIL-005.md) |
| SIL-006 | ground recovers the spacecraft from safe mode | REQ-FAULT-010, REQ-MODE-006 | [SIL-006.md](reports/sil/SIL-006.md) |
| SIL-007 | unreachable mode target refused at validation | REQ-MODE-004, REQ-CMD-006 | [SIL-007.md](reports/sil/SIL-007.md) |
| SIL-008 | safing wins a same-cycle conflict with an accepted command | REQ-EXEC-001 | [SIL-008.md](reports/sil/SIL-008.md) |
| SIL-009 | gyro dropout retreats POINTING to STANDBY | REQ-FAULT-005 | [SIL-009.md](reports/sil/SIL-009.md) |
| SIL-010 | power dropout retreats DOWNLINK to STANDBY | REQ-FAULT-005 | [SIL-010.md](reports/sil/SIL-010.md) |
| SIL-011 | detumble nulls a spinning platform without saturating the wheel | REQ-ADCS-001, REQ-ADCS-003 | [SIL-011.md](reports/sil/SIL-011.md) |
| SIL-012 | a sustained disturbance saturates the wheel and pointing authority is lost | REQ-ADCS-003 | [SIL-012.md](reports/sil/SIL-012.md) |

SIL-011 and SIL-012 run against the plant model (`fsw/sil/plant.cpp`), so they are the only ones whose result depends on measured hardware numbers - inertias from CAD, friction fitted to a coast-down. See [architecture.md](architecture.md).

### Writing a SIL scenario

```yaml
id: SIL-001
title: undervoltage drives safe mode
verifies: [REQ-FAULT-002]
timeline:
  - { t: 100, cmd: { name: SET_MODE, arg: 1, seq: 1 } }
  - { t: 1400, fault: { name: UNDERVOLTAGE, bad: 1 } }
  - { t: 2000 }
expect:
  mode_log:
    - { to: SAFE, trigger: FaultEntry, req: REQ-FAULT-002 }
  acks:
    - { seq: 1, accepted: true }
  final:
    mode: SAFE
    faults_set: [UNDERVOLTAGE]
```

Each timeline step is one executive cycle at time `t` (ms, strictly increasing) and may carry fault samples, one command, both, or nothing. Names come from the catalogs in `common/protocol/state.hpp`. `mode_log` rows match in order and field by field, `acks` match by sequence number against decoded telemetry, `final` checks the end state. Every frame is CRC-checked unconditionally.

Two things to know. The command-loss timer is always armed, so any timeline running quiet past t=5000 safes on COMMAND_LINK_LOSS - keep scenarios short or feed NOOP keep-alives. And do not grade on the final mode alone: pin the trigger and requirement id, because SAFE reached for the wrong reason is still wrong.

## HIL

Same flight software on the real STM32 over the live serial link, scenarios under `fsw/hil/scenarios/`. There is no input timeline - stimuli are bench actions declared as an operator instruction - and the only observable is the telemetry stream, which is the constraint a real ground station works under.

Inside `tools/hil_runner.py`, a pure link monitor owns every health decision (link up, loss against the heartbeat timeout, recovery, sequence gaps, timing) and takes time as an argument, so it is unit-tested with injected time. Around it is a serial pump that only moves bytes. Grading and reports share SIL's core and exit-code contract.

```
just hil                 # the whole campaign
just hil 2               # by number, name fragment, or path
just hil-scope 1 frame   # file a scope capture
```

HIL needs the bench, so it is manual and never runs in CI.

### Catalog

| ID | Title | Verifies | Report |
| -- | ----- | -------- | ------ |
| HIL-001 | heartbeat timing on the live link | REQ-VV-003, REQ-TLM-002, REQ-TLM-003 | [HIL-001.md](reports/hil/HIL-001.md), [scope captures](reports/hil/HIL-001-scope.md) |
| HIL-002 | link loss declared and recovery observed | REQ-VV-003 | [HIL-002.md](reports/hil/HIL-002.md) |
| HIL-003 | scheduler smoke - every task alive, stacks intact, watchdog fed | REQ-RT-002, REQ-RT-003, REQ-WDG-001 | [HIL-003.md](reports/hil/HIL-003.md) |

Three things about these that are not obvious from the reports:

- HIL-001's heartbeat band is 0.9-1.15 s and is deliberately not tighter. The board measures 1.000 s, but the window has to absorb USB CDC arrival jitter on the host, which is not the board's fault to be graded on.
- HIL-003's `stack_free_min` is a floor, not a measurement. Stacks are sized at 4-5x the observed peak, so dipping under 64 free words means a task's work grew and the sizing needs redoing (REQ-RT-003).
- The watchdog actually biting cannot come from HIL-003 - a run that resets has stopped being a smoke test. It was demonstrated separately by removing the control task's check-in, after which the board reset and reported `reset=iwdg-watchdog`.

Scope captures are the one kind of HIL evidence the runner cannot produce. They are taken at the bench, filed under `docs/reports/hil/img/`, and written up in a hand-authored companion (`HIL-001-scope.md`) beside the generated report, which the runner overwrites on every rerun.

### Writing a HIL scenario

```yaml
id: HIL-002
title: link loss declared and recovery observed
verifies: [REQ-VV-003]
duration_s: 45
operator: about 10 s in, hold the reset button down for about 10 s, then release
expect:
  link: recovered
  outage_s: { min: 5.0 }
```

`duration_s` is the observation window; `operator` is printed at start. Expectations: `link` (`up` = came up once and never dropped, `recovered` = one loss and one recovery), `seq_gaps`, `crc_rejects`, `period_s` (a band every measured period must sit in), `outage_s`, `tasks_reported` (the exact set of task names in every report), `watchdog` (`fed` = no report withheld the pet), `stack_free_min`. An expectation left out is not graded - HIL-002 omits `seq_gaps` because the reboot's counter restart is expected.
