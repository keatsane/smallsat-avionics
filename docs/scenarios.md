# SIL scenarios

A scenario is a declared test of the flight software: an input timeline, the expected response, and the requirement(s) it verifies, all in one YAML file under `fsw/sil/scenarios/`. The runner (`tools/sil_runner.py`) compiles the timeline into per-cycle input lines, drives the flight software through the SIL shim, grades what actually happened against the expectations, and writes a report to `docs/reports/<id>.md`. Adding a scenario means adding a YAML file - the runner contains no scenario-specific logic.

## Running

```
just sil                                              # the whole suite
just sil fsw/sil/scenarios/sil_005_command_loss.yaml  # one scenario
just sil-shim                                         # one timeline through the bare shim, ungraded
```

Exit code 0 means every check passed, 1 means a scenario failed, 2 means the harness itself hit an error (bad scenario file, shim missing, corrupt frame). Reports are written either way. To hand-feed the shim directly, generate a timeline from any scenario with `python tools/sil_runner.py --compile-only <scenario.yaml>` and pipe it in - timelines are never stored, only generated.

## Catalog

| ID | Title | Verifies | Report |
| -- | ----- | -------- | ------ |
| SIL-001 | undervoltage drives safe mode | REQ-FAULT-002 | [SIL-001.md](reports/SIL-001.md) |
| SIL-002 | illegal-in-mode command is rejected and acked | REQ-CMD-001, REQ-CMD-003 | [SIL-002.md](reports/SIL-002.md) |
| SIL-003 | degraded fault latches without entering safe mode | REQ-FAULT-005 | [SIL-003.md](reports/SIL-003.md) |
| SIL-004 | transient bad samples are debounced away | REQ-FAULT-004, REQ-FAULT-009 | [SIL-004.md](reports/SIL-004.md) |
| SIL-005 | command-link silence trips the dead man | REQ-CMD-002 | [SIL-005.md](reports/SIL-005.md) |
| SIL-006 | ground recovers the spacecraft from safe mode | REQ-FAULT-010, REQ-MODE-006 | [SIL-006.md](reports/SIL-006.md) |
| SIL-007 | accepted command, illegal transition refused | REQ-MODE-004 | [SIL-007.md](reports/SIL-007.md) |
| SIL-008 | safing wins a same-cycle conflict | REQ-EXEC-001 | [SIL-008.md](reports/SIL-008.md) |

### SIL-001 - undervoltage drives safe mode

The marquee fault path, end to end: the satellite is commanded out of BOOT into STANDBY, then three consecutive bad bus-voltage samples are injected. The first two debounce silently; the third latches UNDERVOLTAGE (debounce threshold 3) and the same cycle's fault response commands SAFE. The expectations pin the mode-log row precisely - trigger `FaultEntry`, requirement `REQ-FAULT-002` - so reaching SAFE for any other reason fails the scenario.

### SIL-002 - illegal-in-mode command is rejected and acked

Command validation observed from the outside: `CAPTURE_IMAGE` is legal only in POINTING, so sending it in STANDBY must produce a rejected acknowledgment in telemetry carrying the reason (`IllegalInMode`), and must change nothing. Acks are matched against the decoded telemetry frames, not the internal logs, because the acknowledgment requirement (REQ-CMD-003) is about what the ground can see.

### SIL-003 - degraded fault latches without entering safe mode

The severity tiers doing their job: GYRO_DROPOUT is Degraded, so three bad samples latch it and it shows up in the fault bitmask - but the spacecraft must stay in STANDBY. A fault response that overreacts (safing on a non-critical fault) fails this scenario.

### SIL-004 - transient bad samples are debounced away

Four bad undervoltage samples arrive, but with a good sample in the middle: the streak never reaches the debounce threshold of three, because a good sample resets the count (REQ-FAULT-009). Nothing latches, nothing safes. Without the reset, the fourth bad sample would have tripped it - that is exactly the false-positive this scenario guards against.

### SIL-005 - command-link silence trips the dead man

The command-loss timer end to end: one command at t=100, then nothing. Just past t=5100 the 5-second timeout expires, COMMAND_LINK_LOSS (Critical, debounce 1) latches, and the spacecraft safes itself. This is the autonomous lost-contact behavior - the spacecraft protects itself when the ground goes quiet.

### SIL-006 - ground recovers the spacecraft from safe mode

The full incident arc: undervoltage forces SAFE, then the ground clears the latched fault explicitly (`CLEAR_FAULT` - the only thing that un-latches a fault, REQ-FAULT-010) and commands the return to STANDBY - the single legal exit from SAFE (REQ-MODE-006). Three mode-log rows are pinned in order: the climb out of BOOT, the fault entry, the commanded recovery.

### SIL-007 - accepted command, illegal transition refused

Acceptance is not execution. `SET_MODE POINTING` is a perfectly valid command (known id, in-range argument), so it is accepted and acked - but STANDBY -> POINTING is not in the transition allow-list, so the mode manager refuses it and the mode is unchanged (REQ-MODE-004). The two-stage semantics matter: the ack says "your command was understood", the heartbeat says what actually happened.

### SIL-008 - safing wins a same-cycle conflict

The executive's ordering requirement (REQ-EXEC-001) under fire: in one cycle, the third undervoltage sample latches the fault and an accepted `SET_MODE DETUMBLE` arrives. The safing must win - the spacecraft ends in SAFE, never DETUMBLE, even though the command was validated and acknowledged.

## Writing a scenario

The shape, by example:

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

Each timeline step is one executive cycle at time `t` (milliseconds, strictly increasing); a step may carry fault samples, one command, both, or nothing. Fault and command names come from the catalogs in `common/protocol/state.hpp`; `SET_MODE` and `CLEAR_FAULT` take the target's catalog id as `arg`. Expectations are declarative: `mode_log` rows are matched in order and field-by-field against the observed transitions, `acks` are matched by sequence number against the decoded telemetry frames, and `final` checks the end state. Every telemetry frame the run emits is CRC-checked unconditionally.

Two rules of thumb. First, the command-loss timer is always armed: any timeline that runs quiet past t=5000 enters SAFE on COMMAND_LINK_LOSS (that is SIL-005's whole subject), so scenarios about anything else stay short or feed 1 Hz NOOP keep-alives. Second, never grade on the final mode alone - pin the mode-log trigger and requirement id, because SAFE reached for the wrong reason is a failure that a final-mode check would wave through.
