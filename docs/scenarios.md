# Scenarios

A scenario is a declared test of the flight software with an id, the requirement(s) it verifies, and graded expectations, all in one YAML file. They come at two levels: SIL scenarios drive the flight software on the host from an input timeline, and HIL scenarios watch the same flight software running on the real STM32 over the live serial link. Either way the runner grades observed against expected and writes a report - adding a scenario means adding a YAML file, never runner code.

## SIL

A SIL scenario declares an input timeline and the expected response, under `fsw/sil/scenarios/`. The runner (`tools/sil_runner.py`) compiles the timeline into per-cycle input lines, drives the flight software through the SIL shim, grades what actually happened against the expectations, and writes a report to `docs/reports/sil/<id>.md`.

### Running

```
just sil                # the whole suite
just sil 5              # one scenario by number...
just sil command_loss   # ...or by name fragment (a path works too)
```

Exit code 0 means every check passed, 1 means a scenario failed, 2 means the harness itself hit an error (bad scenario file, shim missing, corrupt frame). Reports are written either way.

For debugging the harness itself, the bare shim also runs interactively (`./fsw/build/sil_shim.exe`, type timeline lines, end with EOF) or piped a hand-picked timeline (`python tools/sil_runner.py --compile-only <scenario.yaml> | ./fsw/build/sil_shim.exe`) - timelines are never stored, only generated.

### Catalog

| ID | Title | Verifies | Report |
| -- | ----- | -------- | ------ |
| SIL-001 | undervoltage drives safe mode | REQ-FAULT-002 | [SIL-001.md](reports/sil/SIL-001.md) |
| SIL-002 | illegal-in-mode command is rejected and acked | REQ-CMD-001, REQ-CMD-003 | [SIL-002.md](reports/sil/SIL-002.md) |
| SIL-003 | degraded fault latches without entering safe mode | REQ-FAULT-005 | [SIL-003.md](reports/sil/SIL-003.md) |
| SIL-004 | transient bad samples are debounced away | REQ-FAULT-004, REQ-FAULT-009 | [SIL-004.md](reports/sil/SIL-004.md) |
| SIL-005 | command-link silence trips the dead man | REQ-CMD-002 | [SIL-005.md](reports/sil/SIL-005.md) |
| SIL-006 | ground recovers the spacecraft from safe mode | REQ-FAULT-010, REQ-MODE-006 | [SIL-006.md](reports/sil/SIL-006.md) |
| SIL-007 | accepted command, illegal transition refused | REQ-MODE-004 | [SIL-007.md](reports/sil/SIL-007.md) |
| SIL-008 | safing wins a same-cycle conflict | REQ-EXEC-001 | [SIL-008.md](reports/sil/SIL-008.md) |

#### SIL-001 - undervoltage drives safe mode

The first end-to-end fault path: the satellite is commanded out of BOOT into STANDBY, then three consecutive bad bus-voltage samples are injected. The first two debounce silently; the third latches UNDERVOLTAGE (debounce threshold 3) and the same cycle's fault response commands SAFE. The expectations pin the mode-log row - trigger `FaultEntry`, requirement `REQ-FAULT-002` - so reaching SAFE for any other reason fails the scenario.

#### SIL-002 - illegal-in-mode command is rejected and acked

Command validation observed from the outside: `CAPTURE_IMAGE` is legal only in POINTING, so sending it in STANDBY must produce a rejected acknowledgment in telemetry carrying the reason (`IllegalInMode`), and must change nothing. Acks are matched against the decoded telemetry frames, not the internal logs, because the acknowledgment requirement (REQ-CMD-003) is about what the ground can see.

#### SIL-003 - degraded fault latches without entering safe mode

ACCEL_GYRO_DROPOUT is Degraded, so three bad samples latch it and it shows up in the fault bitmask, but the spacecraft stays in STANDBY. Safing on a non-critical fault fails this scenario. This pins the no-SAFE half of REQ-FAULT-005; the active POINTING/DETUMBLE -> STANDBY retreat is a separate scenario still to add.

#### SIL-004 - transient bad samples are debounced away

Four bad undervoltage samples arrive, but with a good sample in the middle: the streak never reaches the debounce threshold of three, because a good sample resets the count (REQ-FAULT-009). Nothing latches, nothing safes. Without the reset, the fourth bad sample would have tripped it; this scenario keeps that false positive out.

#### SIL-005 - command-link silence trips the dead man

The command-loss timer end to end: one command at t=100, then nothing. Just past t=5100 the 5-second timeout expires, COMMAND_LINK_LOSS (Critical, debounce 1) latches, and the spacecraft safes itself. This is the autonomous lost-contact behavior - the spacecraft protects itself when the ground goes quiet.

#### SIL-006 - ground recovers the spacecraft from safe mode

The full incident arc: undervoltage forces SAFE, then the ground clears the latched fault explicitly (`CLEAR_FAULT`, REQ-FAULT-010) and commands the return to STANDBY, the single legal exit from SAFE (REQ-MODE-006). Three mode-log rows are pinned in order: the climb out of BOOT, the fault entry, the commanded recovery.

#### SIL-007 - accepted command, illegal transition refused

Acceptance is not execution. `SET_MODE POINTING` is a perfectly valid command (known id, in-range argument), so it is accepted and acked - but STANDBY -> POINTING is not in the transition allow-list, so the mode manager refuses it and the mode is unchanged (REQ-MODE-004). The two-stage semantics matter: the ack says "your command was understood", the heartbeat says what actually happened.

#### SIL-008 - safing wins a same-cycle conflict

The executive's ordering requirement (REQ-EXEC-001): in one cycle, the third undervoltage sample latches the fault and an accepted `SET_MODE DETUMBLE` arrives. The safing path wins - the spacecraft ends in SAFE, never DETUMBLE, even though the command was validated and acknowledged.

### Writing a SIL scenario

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

Two habits keep scenarios honest. First, the command-loss timer is always armed: any timeline that runs quiet past t=5000 enters SAFE on COMMAND_LINK_LOSS (that is SIL-005's whole subject), so scenarios about anything else stay short or feed 1 Hz NOOP keep-alives. Second, do not grade on the final mode alone - pin the mode-log trigger and requirement id, because SAFE reached for the wrong reason is still wrong.

## HIL

A HIL scenario tests the same flight software on the real STM32 over the live serial link, under `fsw/hil/scenarios/`. There is no input timeline: the stimuli are bench actions, declared as an operator instruction in the YAML (hold the reset button, pull a jumper), and the only observable is the telemetry stream itself - exactly what a ground station sees. The runner (`tools/hil_runner.py`) watches the link for the scenario's window through a heartbeat-health monitor, narrates its events live (link up, link loss, recovery, sequence gaps), grades them against the expectations, and writes `docs/reports/hil/<id>.md`.

### Running

```
just hil COM3            # the whole campaign, pausing for the operator between scenarios
just hil COM3 2          # one scenario by number, name fragment, or path
just hil-scope 1 frame   # file a scope capture for a HIL test
```

HIL runs need the bench - the board flashed and on a COM port - so they are manual and never run in CI. Exit codes match SIL: 0 pass, 1 fail, 2 harness error.

### Catalog

| ID | Title | Verifies | Report |
| -- | ----- | -------- | ------ |
| HIL-001 | heartbeat timing on the live link | REQ-VV-003, REQ-TLM-002, REQ-TLM-003 | [HIL-001.md](reports/hil/HIL-001.md), [scope captures](reports/hil/HIL-001-scope.md) |
| HIL-002 | link loss declared and recovery observed | REQ-VV-003 | [HIL-002.md](reports/hil/HIL-002.md) |

#### HIL-001 - heartbeat timing on the live link

A hands-off observation window: the board beacons, the operator touches nothing. Graded: the link comes up exactly once and never drops, zero sequence gaps, zero CRC rejects, and every heartbeat period inside the declared band (0.8-1.3 s - the 1 Hz heartbeat is gated on a ~100 ms super-loop, so the period quantizes; fixed-rate execution is REQ-RT-002, phase 6). The wire-level numbers - burst period, frame duration, inter-byte gap - are measured on the scope and live in the [capture companion](reports/hil/HIL-001-scope.md).

#### HIL-002 - link loss declared and recovery observed

The ground side of the link-health design: mid-window the operator holds the board's reset button (true heartbeat silence on the ST-LINK VCP - the firmware stops while the COM port stays alive), and the host must declare link loss strictly past 5 s of silence, then observe the recovery and its measured outage when the button is released. The reboot restarts the sequence counter, which shows up as an ungraded seq_gap event with a negative miss count - a backwards sequence jump is the signature of a spacecraft reset, not packet loss, and becomes a formally reported boot event with REQ-WDG-002 (phase 6).

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

`duration_s` is the observation window; `operator` is the human's script for the run, printed at start. Expectations: `link` (`up` = came up once and never dropped, `recovered` = exactly one loss and one recovery), `seq_gaps` (total missed packets), `crc_rejects`, `period_s` (a min/max band every measured heartbeat period must sit in), and `outage_s` (minimum measured outage). An expectation left out is not graded - HIL-002 omits `seq_gaps` on purpose because the reboot's counter restart is expected.
