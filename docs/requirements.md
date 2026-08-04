# Requirements

Running requirements for the avionics stack. Each requirement has an ID, a statement, a status, and a verification method (test, analysis, inspection, or demonstration); once there is evidence, the artifact is listed too. Requirements are written ahead of the code they govern, so many start as planned until their phase lands. The status, not the existence of the requirement, tracks reality: planned -> in progress -> unit-verified -> SIL-verified -> bench-verified -> HIL-verified. A requirement whose verification line lists methods beyond its current status still owes that evidence.

| Section | Covers |
| ------- | ------ |
| [Mode management](#mode-management) | the six modes, the legal transitions between them, and the transition log |
| [Fault management](#fault-management) | the detect - debounce - latch - respond pipeline, severities, and the degraded fallbacks |
| [Executive](#executive) | the once-per-cycle ordering, including which side wins a same-cycle conflict |
| [Comms - command uplink and telemetry downlink](#comms---command-uplink-and-telemetry-downlink) | command validation and acknowledgment, the heartbeat, and the command-loss timer |
| [Real-time execution and recovery](#real-time-execution-and-recovery) | the fixed-rate control cycle, task health, stack margins, and the watchdog |
| [Sensors and data validity](#sensors-and-data-validity) | validity flags, staleness, and disagreement between redundant sources |
| [Status indication](#status-indication) | the three-bead WS2812 array - mode, fault severity, and link health |
| [Attitude control (ADCS)](#attitude-control-adcs) | detumble and pointing against the reaction wheel |
| [Payload](#payload) | image capture, the camera dropout fault, and the chunked downlink |
| [Platform abstraction and portability](#platform-abstraction-and-portability) | the boundary that lets the same flight software run on the host and on the STM32 |
| [Verification and traceability](#verification-and-traceability) | the scenario harnesses and the requirement-to-artifact chain |

## Mode management

The spacecraft operates in exactly one of six modes at any time. Their intent:

| Mode | Purpose |
| ---- | ------- |
| BOOT | power-on and self-checks |
| STANDBY | idle and healthy, awaiting commands |
| DETUMBLE | reduce body rates after deployment or upset |
| POINTING | hold a commanded attitude |
| DOWNLINK | empty the onboard data buffer to the ground during a contact pass |
| SAFE | minimal, power-conservative state after a critical fault |

**REQ-MODE-001** - The flight software shall represent the current operating mode as exactly one value of the set {BOOT, STANDBY, DETUMBLE, POINTING, DOWNLINK, SAFE} at all times.  
**Type**: Functional  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_mode_manager.cpp

**REQ-MODE-002** - On power-on or reset, the flight software shall initialize the current mode to BOOT before permitting any transition, and have an empty log.  
**Type**: Functional  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_mode_manager.cpp

**REQ-MODE-003** - The flight software shall permit only the autonomous mode transitions listed below. Any transition not listed - including a transition from a mode to itself - shall be treated as illegal.

| From | Legal transitions to |
| ---- | -------------------- |
| BOOT | STANDBY, DETUMBLE, SAFE |
| STANDBY | DETUMBLE, POINTING, DOWNLINK, SAFE |
| DETUMBLE | STANDBY, POINTING, DOWNLINK, SAFE |
| POINTING | STANDBY, DETUMBLE, DOWNLINK, SAFE |
| DOWNLINK | STANDBY, DETUMBLE, POINTING, SAFE |
| SAFE | none autonomously (ground-commanded to STANDBY only - see REQ-MODE-006) |

The operating modes form a clique - each reaches every other. An earlier table forced sequencing hops (STANDBY could not reach POINTING without passing through DETUMBLE), which encoded a deployment story this bench rig does not have and cost a command per hop. What stays restricted is the frame of the graph: BOOT is left only by passing self-checks, SAFE only by ground command.

**Type**: Functional  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_mode_manager.cpp

**REQ-MODE-004** - On a request for a transition that is not in the legal set, the flight software shall leave the current mode unchanged and shall not append a transition log entry.  
**Type**: Functional  
**Status**: SIL-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/test/test_mode_manager.cpp, docs/reports/sil/SIL-007.md

**REQ-MODE-005** - Every operating mode (BOOT, STANDBY, DETUMBLE, POINTING, DOWNLINK) shall have a legal transition to SAFE, so a fault response can command SAFE from any operating mode.  
**Type**: Functional  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_mode_manager.cpp

**REQ-MODE-006** - The flight software shall perform no autonomous transition out of SAFE. The only permitted exit from SAFE shall be a ground-commanded transition to STANDBY.  
**Type**: Functional  
**Status**: SIL-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/test/test_mode_manager.cpp, docs/reports/sil/SIL-006.md

**REQ-MODE-007** - The flight software shall record, for every transition, the trigger that caused it, drawn from the set {PowerOn, Nominal, FaultEntry, FaultCleared, Timeout, Command}.  
**Type**: Functional  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_mode_manager.cpp

**REQ-MODE-008** - On every accepted transition, the flight software shall append one record to the mode transition log containing: the platform timestamp in milliseconds, the trigger, the previous mode, the new mode, and the requirement ID the transition serves.  
**Type**: Functional  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_mode_manager.cpp

**REQ-MODE-010** - The flight software shall leave BOOT autonomously, entering STANDBY once every fault detector has had a full debounce window to disqualify the vehicle and none has latched a Critical fault. The transition shall carry the Nominal trigger. BOOT shall never be a resting state: absent a Critical fault the vehicle proceeds, and given one it safes.  
**Type**: Functional  
**Status**: unit-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/src/executive.cpp, fsw/test/test_executive.cpp

The wait is deliberate rather than an immediate hop on the first cycle. A fault needs `debounce_n` consecutive bad samples to latch, so leaving BOOT any sooner would mean declaring the self-check passed before a persistently bad sensor could possibly have failed it.

**REQ-MODE-011** - The flight software shall exit DETUMBLE autonomously, entering STANDBY once the measured body rate has remained inside the controller's deadband for one second of consecutive samples. The transition shall carry the Nominal trigger. A sample outside the deadband shall reset the count.
**Type**: Functional
**Status**: SIL-verified (SIL-013 - the vehicle nulls a 1 rad/s spin and steps down on its own, with the Nominal trigger and this requirement id in the log row. SIL-011 ends before the exit can fire and still expects DETUMBLE, so the pair pins both halves: converge first, then leave)
**Verification**: SIL, then HIL once the wheel is back
**Artifact**: fsw/src/executive.cpp, docs/reports/sil/SIL-013.md

**REQ-MODE-012** - The flight software shall enter DETUMBLE autonomously from any operating mode other than SAFE and POINTING when the measured body rate exceeds a configured threshold for three consecutive samples, shall remember the interrupted mode, and shall return to it when the detumble completes. The transitions shall carry the Nominal trigger.
**Type**: Functional
**Status**: SIL-verified (SIL-014 - a 2.5 rad/s shove in STANDBY pulls the vehicle into DETUMBLE on its own and it resumes STANDBY when nulled; the resume of an interrupted DOWNLINK is unit-verified. The shove has to beat bearing friction to the threshold, which sheds ~2.9 rad/s per second on its own - on this rig autonomy only sees the upsets friction cannot handle first)

**POINTING is excluded, and the rig is why (2026-08-04).** A slew to a new bearing is body rate on purpose, and it passes the entry threshold long before it reaches the target - so the vehicle interrupted its own manoeuvre, detumbled the rate its own controller had asked for, resumed POINTING, and started again. The bench showed forty seconds of that without ever arriving. The rule underneath: this requirement answers rate *nobody asked for*. In POINTING the rate is the controller's own output, and bounding it belongs to the pointing law's damping term, not to a mode change that discards the target. SAFE is excluded for the older reason - a safed vehicle sheds activity, and spinning quietly is what SAFE already accepts.
**Verification**: unit test, SIL, then HIL once the wheel is back
**Artifact**: fsw/src/executive.cpp, fsw/test/test_executive.cpp, docs/reports/sil/SIL-014.md

**REQ-MODE-009** - The mode transition log shall be a fixed-capacity buffer that allocates no memory dynamically and retains at least the 32 most recent records; when full, the oldest record shall be overwritten.  
**Type**: Constraint  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_mode_manager.cpp

## Fault management

Every fault carries a severity that drives its response:

| Severity | Response when latched |
| -------- | --------------------- |
| Warning | reported in telemetry; off-nominal but still fully capable; no failover |
| Degraded | a capability was lost but a documented fallback exists; switch to it and keep operating; does not force SAFE |
| Critical | a capability was lost with no fallback; forces SAFE if unresolved |

The live fault catalog (command-link loss, IMU dropouts, power-monitor dropout, undervoltage, overvoltage, and overcurrent) is defined once in `common/protocol/state.hpp`; a fault's position there is both its id and its bit in the active-fault bitmask. Planned future capabilities add their own faults when the detector code lands.

**REQ-FAULT-001** - The fault manager shall latch an active fault until it is explicitly cleared; a condition that clears on its own shall not un-latch the fault.  
**Type**: Functional  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_fault_manager.cpp

**REQ-FAULT-002** - A latched Critical fault shall command SAFE, and only after its debounce window so a transient sample cannot force it.  
**Type**: Functional  
**Status**: bench-verified (the bench supply was walked down from 14.8 V; UNDERVOLTAGE latched at 13.6 V and the vehicle entered SAFE within one heartbeat, with the mode and fault beads following)  
**Verification**: unit test, SIL scenario (undervoltage -> SAFE), then a bench sweep  
**Artifact**: fsw/test/test_fault_manager.cpp, docs/reports/sil/SIL-001.md (scenario SIL-001), docs/journal.md

**REQ-FAULT-003** - Each fault shall have a defined policy - severity, debounce threshold, and owning requirement - held in a single fault table that is the only place fault policy is written; a compile-time check shall keep that table sized to the fault catalog.  
**Type**: Constraint  
**Status**: unit-verified  
**Verification**: inspection and unit test (compile-time size check)  
**Artifact**: fsw/src/fault_manager.cpp

**REQ-FAULT-004** - A fault shall latch only after its debounce threshold of consecutive bad samples is reached; a single transient bad sample shall never latch it.  
**Type**: Functional  
**Status**: SIL-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/test/test_fault_manager.cpp, docs/reports/sil/SIL-004.md

**REQ-FAULT-005** - A fault of Warning or Degraded severity shall not by itself command SAFE; a Degraded fault shall switch to its documented fallback behavior (see Degraded fallback behaviors below), and every such fault shall be latched and reported in telemetry.
**Type**: Functional
**Status**: SIL-verified (SIL-003 for the no-SAFE half; SIL-009 and SIL-010 for the retreats. The WHEEL_DROPOUT retreat is unit-verified only - same code path from the same two modes as the gyro retreat, so a third scenario would restate SIL-009 with one name changed)
**Verification**: unit test and SIL  
**Artifact**: fsw/src/executive.cpp, fsw/src/sensor_monitor.cpp, fsw/test/test_fault_manager.cpp, fsw/test/test_executive.cpp, fsw/test/test_sensor_monitor.cpp, docs/reports/sil/SIL-003.md, docs/reports/sil/SIL-009.md, docs/reports/sil/SIL-010.md

### Degraded fallback behaviors

REQ-FAULT-005 requires a Degraded fault to switch to its documented fallback - listed below - rather than forcing SAFE. A Warning fault such as `MAG_DROPOUT` latches and reports without changing mode.

| Fault | Fallback when latched |
| ----- | --------------------- |
| ACCEL_GYRO_DROPOUT | In POINTING or DETUMBLE - the modes that depend on body-rate feedback - retreat to STANDBY, a stable idle; holding attitude without the gyro is unsafe, but the loss is not mission-ending. In any other mode, latch and report only. |
| POWER_DROPOUT | In POINTING, DETUMBLE, or DOWNLINK - the high-power modes - retreat to STANDBY; with the power monitor unreadable, running power-hungry operations blind to brownout/overcurrent is unsafe, and STANDBY's lower draw cuts the risk. In any other mode, latch and report only. |
| WHEEL_DROPOUT | In POINTING or DETUMBLE - the two modes that actuate - retreat to STANDBY; with the reaction wheel's link down a torque command goes nowhere, so holding a mode the rig cannot fly is a false claim of control. In any other mode, latch and report only. |

The active retreats - POINTING/DETUMBLE -> STANDBY on ACCEL_GYRO_DROPOUT and on WHEEL_DROPOUT, and POINTING/DETUMBLE/DOWNLINK -> STANDBY on POWER_DROPOUT - run in the executive's fault-response step and are logged as `FaultEntry` transitions stamped REQ-FAULT-005. Recovery is ground-commanded like every fault (CLEAR_FAULT, REQ-FAULT-010); the planned RESET_DEVICE command will let the ground re-initialize the affected sensor before clearing it.

`WHEEL_DROPOUT` is detected from the silence on the wheel link rather than from a sample: the ESC beacons its status every 250 ms, so a second without one is a dead link. The first frame after boot gets a longer window (8 s) because the ESC aligns its FOC for about five seconds from cold and says nothing until it finishes - without that grace every boot would latch a dropout that clears itself a moment later, which is how a real fault gets trained into background noise. An ESC that never comes up still latches once the grace expires.

**REQ-FAULT-006** - The flight software shall evaluate the full fault set once per control cycle and apply the required response; when more than one response is indicated, the most conservative one shall win (SAFE dominates).  
**Type**: Functional  
**Status**: unit-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/test/test_fault_manager.cpp

**REQ-FAULT-007** - The flight software shall define the fault catalog once, in common/protocol/state.hpp, as a single index enum; a fault's position in that list shall be both its id and its bit position in the active-fault bitmask.  
**Type**: Interface  
**Status**: unit-verified  
**Verification**: inspection  
**Artifact**: common/protocol/state.hpp

**REQ-FAULT-008** - The set of currently latched faults shall be exposed as a bitmask (bit n set when fault id n is latched); this bitmask is what the heartbeat carries (see REQ-TLM-002).  
**Type**: Interface  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_fault_manager.cpp

**REQ-FAULT-009** - A good (healthy) sample shall reset a fault's consecutive-bad-sample count, but shall not clear a fault that has already latched (see REQ-FAULT-001).  
**Type**: Functional  
**Status**: SIL-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/test/test_fault_manager.cpp, docs/reports/sil/SIL-004.md

**REQ-FAULT-010** - A latched fault shall be cleared only by an explicit action - a ground command or a defined recovery sequence - never autonomously by the flight software.  
**Type**: Functional  
**Status**: SIL-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/test/test_fault_manager.cpp, fsw/test/test_executive.cpp, docs/reports/sil/SIL-006.md

**REQ-FAULT-011** - The fault manager shall maintain a time-ordered log of fault state-change events; each entry shall record the platform timestamp, the fault id, and whether the fault latched or cleared. Only edges shall be logged - never per-sample - and the log shall be a fixed-capacity ring that overwrites its oldest entry when full.  
**Type**: Functional  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_fault_manager.cpp

**REQ-FAULT-012** - It shall be possible to inhibit an individual fault's autonomous response for ground testing. An inhibited fault shall still debounce, latch, log, and report in telemetry; only the mode change it would command shall be suppressed. The inhibited set shall be reported, so that a run with inhibits in force cannot be mistaken for one without.  
**Type**: Functional  
**Status**: unit-verified (compile-time inhibit list declared at the platform boundary, announced in the boot banner, and carried in every heartbeat; ground-commandable inhibits wait on the ground station in Phase 8)  
**Verification**: unit test  
**Artifact**: fsw/src/fault_manager.cpp, fsw/src/executive.cpp, fsw/test/test_fault_manager.cpp, fsw/test/test_comms.cpp, common/protocol/msg.hpp, tools/ground/frames.py

Inhibits exist because the bench rig is routinely missing subsystems the flight configuration assumes: with no ground station, COMMAND_LINK_LOSS safes the vehicle about five seconds after every boot. Suppressing the *response* while keeping the *evidence* is the standard fault-protection inhibit used during spacecraft commissioning, and it is why the fault still appears in every heartbeat. WHEEL_DROPOUT was inhibited alongside it while the ESC was out of the stack; it came off on 2026-08-03 when the replacement board joined the link, which is the list working as intended - an inhibit is meant to be retired by hardware arriving, not to become permanent.

The inhibited set rides in the heartbeat as its own bitmask, in the same bit layout as the active-fault mask. Announcing it once in the boot banner is not enough: a console attached mid-run, or a log read later, shows two latched faults with the banner long out of scrollback and nothing on the wire saying their responses were suppressed. The status array distinguishes them by colour, but only to someone in the room. Carrying it in telemetry is what makes a saved log self-describing.

## Executive

**REQ-EXEC-001** - The flight software shall process each control cycle's inputs in a fixed, documented order: fault-sample ingestion, command validation, fault response, command dispatch. The fault response shall take precedence: a command accepted in the same cycle a critical fault forces SAFE shall not override the SAFE entry. Command acceptance acknowledges validation only; execution outcome is observed through telemetry. Validation is expected to reject what it can already determine is impossible, including an unreachable SET_MODE target (REQ-CMD-006) - what acceptance cannot promise is immunity from a fault response raised later in the same cycle.  
**Type**: Functional  
**Status**: SIL-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/src/executive.cpp, fsw/test/test_executive.cpp, docs/reports/sil/SIL-008.md

## Comms - command uplink and telemetry downlink

**REQ-CMD-001** - The flight software shall validate every command - known id, in-range parameters, and legal in the current mode - before acting on it; an invalid command shall be rejected and reported, never executed.  
**Type**: Functional  
**Status**: SIL-verified (the uplink path onto the OBC is written - both links are drained each cycle and decoded commands enter as cycle inputs - but has not been exercised on the bench, so validation remains host-only evidence)  
**Verification**: unit test and SIL  
**Artifact**: fsw/test/test_comms.cpp, obc/Src/main.cpp, tools/uart_monitor.py, docs/reports/sil/SIL-002.md (scenario SIL-002)

**REQ-CMD-002** - Loss of ground command contact for longer than a defined timeout shall raise COMMAND_LINK_LOSS.  
**Type**: Functional  
**Status**: SIL-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/test/test_comms.cpp, docs/reports/sil/SIL-005.md

**REQ-CMD-005** - BOOT shall not be a commandable mode. A SET_MODE request naming BOOT shall be rejected as a bad argument.  
**Type**: Functional  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/src/comms/command_handler.cpp, fsw/test/test_comms.cpp

**REQ-CMD-006** - A SET_MODE request naming a mode that is not reachable from the current mode shall be rejected as illegal-in-mode at validation, rather than accepted and then refused by the mode manager.  
**Type**: Functional  
**Status**: SIL-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/src/comms/command_handler.cpp, fsw/test/test_comms.cpp, docs/reports/sil/SIL-007.md

This extends REQ-CMD-005's reasoning from one mode to the whole transition table: an acknowledgement that says yes while the vehicle does not move is a worse interface than a refusal that names why. It narrows REQ-EXEC-001 rather than contradicting it - acceptance still is not execution, because a safing raised in the same cycle overrides an accepted command (SIL-008). What changed is that a refusal already knowable at validation time is now reported at validation time.

BOOT means "powered on and still self-checking" - a state the vehicle enters by resetting and leaves by passing its checks (REQ-MODE-010). There is nothing for the flight software to do with a request to re-enter it. Validating it away matters because the alternative is worse than useless: `BOOT` is a real mode id and a legal transition target from nowhere, so without this check the command passes validation, is acknowledged as accepted, and then changes nothing - an ack that says yes while the vehicle does not move.

**REQ-CMD-003** - Every command shall be acknowledged in telemetry as accepted or rejected, with a reason given on rejection. A command repeating the previous sequence number shall be answered with the previous verdict and shall not be executed again.  
**Type**: Functional  
**Status**: SIL-verified (both verdicts observed as decoded telemetry frames; the retransmission rule is unit-verified. HIL still owed)  
**Verification**: unit test (ack construction, retransmission), SIL and HIL  
**Artifact**: fsw/test/test_comms.cpp, docs/reports/sil/SIL-002.md (scenario SIL-002)

The retransmission rule exists because the ground resends: the LoRa link is half duplex, the vehicle cannot hear anything while its own beacon is on the air, and a command sent into that window is simply not received. The ground console retransmits until it sees an ack, which means a command whose *ack* was lost arrives twice - so the vehicle answers the second copy without acting on it.

**REQ-CMD-004** - Every handled command shall be appended to a command event log recording the platform timestamp, the command id, the verdict, and the rejection reason. The log shall be a fixed-capacity ring that allocates no memory dynamically and overwrites its oldest entry when full.  
**Type**: Functional  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_comms.cpp

**REQ-TLM-001** - All telemetry shall be framed with a sync header, message id, length, and CRC-16; a corrupted or mis-framed packet shall be detected and discarded, never acted on.  
**Type**: Interface  
**Status**: HIL-verified (live framing on the real wire, every frame CRC-checked with zero rejects over the window; the discard path itself is unit-verified)  
**Verification**: unit test and HIL  
**Artifact**: common/protocol/frame.cpp, fsw/test/test_comms.cpp, docs/reports/hil/HIL-001.md, docs/reports/hil/HIL-001-scope.md

**REQ-TLM-002** - The flight software shall emit a periodic heartbeat carrying uptime, a sequence number, the current mode, and the active-fault bitmask.  
**Type**: Functional  
**Status**: HIL-verified (1 Hz emission decoded live from the STM32 and timed on the scope; the on-wire frame carries real mode and fault state)  
**Verification**: unit test, SIL and HIL  
**Artifact**: fsw/test/test_comms.cpp, docs/reports/sil/SIL-001.md, docs/reports/hil/HIL-001.md, docs/reports/hil/HIL-001-scope.md

**REQ-TLM-003** - The heartbeat sequence number shall increment monotonically so the ground can detect dropped packets.  
**Type**: Functional  
**Status**: HIL-verified (59 consecutive live periods with zero gaps; a board reset is detected as a sequence discontinuity in HIL-002)  
**Verification**: unit test; demonstration (scope/host decode)  
**Artifact**: fsw/test/test_comms.cpp, docs/reports/hil/HIL-001.md, docs/reports/hil/HIL-002.md

**REQ-TLM-004** - The telemetry transport shall be swappable behind the frame format - UART first, radio later - with no change to the wire format.  
**Type**: Constraint  
**Status**: demonstrated untethered (2026-07-30 - the vehicle on bench power with no data cable attached: commanded into POINTING over LoRa, a capture and a downlink over the nRF24, 137 chunks reassembled to `captures/image_0017.jpg` with intact JPEG markers. The first pass delivered 72% and two selective-repeat rounds named and refilled the rest, which is the point of the protocol: delivery variance stopped mattering. One encoded frame crosses four transports unchanged; LoRa carries heartbeats, acks and attitude only - the full stream would want 2.8 s of air time per second)
**Verification**: inspection and HIL  
**Artifact**: common/protocol/frame.cpp (the format, unchanged per transport), fsw/platform/stm32/platform_stm32.cpp (one call, three transports), obc/Src/devices/rfm95.c, obc/Src/freertos/telemetry_task.cpp, obc/Src/drivers/uart.c

**REQ-TLM-006** - The ground shall be able to request a single frame of a named telemetry kind over the low-rate link. The full sensor stream shall not ride that link.
**Type**: Functional
**Status**: demonstrated over the air (2026-07-31 - `poll POWER` from the ground console returned one POWER frame over the beacon, on battery power with no cable: bus 16.68 V, 108 mA. The stream restriction is arithmetic: the full stream wants ~2.8 s of air time per second of flight)
**Verification**: unit test, then demonstration over the radio
**Artifact**: fsw/src/executive.cpp, fsw/test/test_executive.cpp, obc/Src/freertos/telemetry_task.cpp

**REQ-TLM-005** - The spacecraft shall indicate current mode, worst latched fault severity with a count of latched faults, and command-link state on a local status display, carrying the same information as the heartbeat packet so the two can be read against each other.  
**Type**: Functional  
**Status**: bench-verified (2026-07-30 - two faults latched and inhibited, the bead held blue and winked twice per cycle against a heartbeat naming exactly those two. Inhibited faults are counted: they are latched, which is what the requirement asks)  
**Verification**: demonstration (drive each mode, latch faults of each severity, and drop the uplink; observe the display against the decoded heartbeat)  
**Artifact**: obc/Src/freertos/control_task.cpp, obc/Src/devices/ws2812.c

## Real-time execution and recovery

**REQ-RT-001** - The flight software shall maintain a monotonic millisecond time base, used to timestamp every logged event.  
**Type**: Performance  
**Status**: bench-verified  
**Verification**: demonstration and inspection  
**Artifact**: obc/Src/drivers/systick.c, obc/Src/freertos/control_task.cpp (passes millis into each cycle)

**REQ-RT-002** - The decision paths shall run at a fixed, bounded rate and shall allocate no memory dynamically (fixed-capacity containers only).  
**Type**: Constraint  
**Status**: HIL-verified (HIL-003: 59 heartbeat periods, mean 1.000 s, the spread host-side USB jitter. The super-loop it replaced was a structural 2% slow. The rate held through a 128-chunk downlink with no cycle overrunning - the worst is the one a capture completes on, where the JPEG end-marker scan costs ~16 ms of 100. Static allocation only)  
**Verification**: inspection and analysis, plus HIL (measure heartbeat spacing idle and under downlink load)  
**Artifact**: fsw/include/fsw/inputs.hpp and fsw/include/fsw/fault_manager.hpp (fixed-capacity ETL containers, no heap), `configSUPPORT_DYNAMIC_ALLOCATION 0` in obc/Inc/FreeRTOSConfig.h, the control task's `xTaskDelayUntil` in obc/Src/freertos/control_task.cpp, docs/reports/hil/HIL-003.md

**REQ-RT-003** - Under concurrent workloads the flight software shall run as prioritized tasks with the control loop at the highest priority, and shall report task health (liveness and stack high-water).  
**Type**: Performance  
**Status**: HIL-verified (HIL-003, 60 s untouched: every task present in all 60 reports, watchdog fed on all of them, worst stack margin 104 free words against a 64 floor. Stacks are sized at ~2.5x measured peaks - generous rather than tight, because the kernel's high-water figure counts a fill pattern and reads optimistic when a written byte happens to hold it)  
**Verification**: HIL (scheduler-smoke run)  
**Artifact**: the task table in obc/Inc/rtos_tasks.h; obc/Src/freertos/; `task_health_t` in common/protocol/msg.hpp; docs/reports/hil/HIL-003.md
**Artifact**: the task table in obc/Inc/rtos_tasks.h; obc/Src/freertos/; `task_health_t` in common/protocol/msg.hpp with its ground decoder and 6 pytest cases

**REQ-RT-004** - An unhandled processor fault exception (hard fault, memory-management, bus, or usage fault) shall be caught by a dedicated handler that captures the fault context and performs a controlled reset, rather than leaving the processor halted in a default infinite loop.  
**Type**: Functional  
**Status**: bench-verified (forced a fault on the bench with a `udf` undefined instruction; the handler dumped pc=0x0800320c, CFSR=0x00010000 (UFSR.UNDEFINSTR) and HFSR=0x40000000 (FORCED - escalated to HardFault), then rebooted via NVIC_SystemReset, the next boot reading reset=software)  
**Verification**: demonstration (trigger a fault, observe the capture then reset)  
**Artifact**: obc/Src/drivers/fault.c

**REQ-WDG-001** - An independent hardware watchdog shall reset the on-board computer if the flight software stops servicing it within the watchdog window.  
**Type**: Functional  
**Status**: HIL-verified (service: HIL-003 fed the watchdog in all 60 reports of a 60 s window, each gated on every deadline task checking in. Bite: withholding the control task's check-in reset the board, and the next banner read `reset=iwdg-watchdog`. The withheld pet appears in telemetry first, which is what makes the reset attributable)
**Verification**: HIL (demonstrated reset)  
**Artifact**: obc/Src/drivers/iwdg.c, obc/Src/freertos/health_task.cpp

**REQ-WDG-002** - After any reset the flight software shall report the reset cause - including a watchdog reset - in a boot telemetry packet.  
**Type**: Functional  
**Status**: bench-verified (2026-07-30 - a cold start emitted exactly one `BOOT t=1125 ms reset=POWER_ON clk=180000000 Hz` frame, decoded from the packet rather than read off the banner. It crosses the PAL as a first-cycle input, not a side channel. Pin reset and watchdog reset were each demonstrated on the banner)  
**Verification**: HIL  
**Artifact**: obc/Src/drivers/reset.c, obc/Inc/drivers/reset.h, obc/Src/freertos/control_task.cpp, common/protocol/msg.hpp, tools/ground/frames.py

## Sensors and data validity

**REQ-SNS-001** - Every sensor sample shall be tagged with an acquisition timestamp and a validity flag; the flight software shall never use a sample marked invalid.  
**Type**: Functional
**Status**: bench-verified (the IMU sample carries an acquisition timestamp and per-half validity flags, streaming live on the bench; the sensor monitor treats an invalid half as a dropout rather than consuming it - a full attitude consumer that honors the flag lands with ADCS)
**Verification**: unit test and HIL
**Artifact**: common/protocol/msg.hpp (imu_data_t), obc/Src/devices/icm20948.c, fsw/src/sensor_monitor.cpp, fsw/test/test_sensor_monitor.cpp

**REQ-SNS-002** - A sensor whose data is invalid, missing, or frozen beyond a defined staleness window shall raise that sensor's dropout fault.
**Type**: Functional
**Status**: unit-verified (dropouts raised on invalid or frozen sources. Both invalid-source paths are bench-exercised: pulling the IMU latches ACCEL_GYRO_DROPOUT + MAG_DROPOUT, and POWER_DROPOUT latched while the INA228 was unreachable. **Owed: HIL**)
**Verification**: unit test and HIL
**Artifact**: fsw/src/sensor_monitor.cpp, fsw/test/test_sensor_monitor.cpp

**REQ-SNS-003** - Where redundant sources exist, disagreement beyond a defined threshold shall raise a dedicated disagreement fault added with that redundant sensor path.  
**Type**: Functional  
**Status**: deferred - conditional, and its condition is not met. The requirement is written to fire "where redundant sources exist" and this build has none: one IMU, one power monitor, one temperature sensor, one camera. Deliberately kept rather than deleted, because it is the rule any second sensor arrives under; deliberately not "planned", because planned implies work is queued and nothing here is buildable until a redundant path exists.  
**Verification**: unit test and SIL, when a redundant path exists

**REQ-SNS-004** - A valid monitored power reading outside its configured operating limits shall raise the corresponding power fault (undervoltage, overvoltage, overcurrent).
**Type**: Functional
**Status**: unit-verified; **HIL not planned** (2026-08-04). Each power fault is raised when its reading crosses the configured limit after debounce. An earlier bench run latched UNDERVOLTAGE live against the low-side monitor, but it has since moved high-side onto the 14.8 V bus and the limits were retuned, so that evidence does not cover this build.

Re-running it was considered and declined, for reasons worth stating rather than leaving as a gap. **Overcurrent cannot be tested at all here**: tripping 1.5 A needs a load bank the bench does not have, and the vehicle's own draw never approaches it. **Undervoltage and overvoltage would require disconnecting the flight battery and rigging a variable supply in its place**, which is bench work with a real chance of disturbing a rig that currently flies. The fault logic itself is exercised every run by the unit suite and by SIL-001, which drives an undervoltage to SAFE; what is missing is only the ADC-to-fault path on this specific wiring, and that is a narrower claim than the requirement being unverified.
**Verification**: unit test and HIL
**Artifact**: fsw/src/sensor_monitor.cpp, fsw/test/test_sensor_monitor.cpp

**REQ-SNS-005** - A valid monitored temperature reading outside its configured operating limits shall raise the corresponding temperature fault (overtemperature, undertemperature).
**Type**: Functional
**Status**: unit-verified; **HIL not planned** (2026-08-04). Both crossings latch after debounce, TEMP_DROPOUT on an invalid sample. Overtemperature is Critical -> SAFE; undertemperature is a report-only Warning, since a low-power state would only cool the spacecraft further.

Forcing a real crossing means heating the TMP117 to 60 C while it sits on a plate of PLA next to the flight electronics. A heat gun does that to more than the sensor, and risking a working vehicle to demonstrate a threshold comparison the unit tests already cover is a poor trade. Declined deliberately rather than left owed.
**Verification**: unit test and HIL
**Artifact**: fsw/src/sensor_monitor.cpp, fsw/test/test_sensor_monitor.cpp

## Status indication

**REQ-HMI-001** - The spacecraft shall indicate its current mode, the worst active fault severity, and its command-link health on an external visual indicator, updated every control cycle and readable without a ground connection.  
**Type**: Functional  
**Status**: bench-verified (three WS2812 beads from PA8 by TIM1_CH1 + DMA: mode by colour, the fault ladder, and the uplink as blinking amber before first contact, green in contact, red past the 5 s timeout. Bit timing scope-confirmed at 0.4/0.8 us on a 1.25 us period)
**Verification**: demonstration (observe the array follow a commanded mode change and a latched fault)  
**Artifact**: obc/Src/drivers/pwm_dma.c, obc/Src/devices/ws2812.c, obc/Src/main.cpp, docs/journal.md (the dated observation)

### Mode bead colors

Bead 0 carries the mode, one color each, no blinking - a spinning platform is read at a glance and a blink code is not.

**Motion is deliberately rationed across the strip.** Bead 1 spends it on a number (the latched-fault count) and bead 2 spends it on one persistent state (never-acquired). Nothing else blinks, and that is a decision rather than an omission: a strip where every bead moves cannot be counted, and the count is the only thing here carrying data rather than a category. DOWNLINK was considered for a mode blink and rejected - since the payload downlink became self-pacing a whole image leaves in about a second, so the blink would be over before anyone looked up.

| Mode | Color |
| ---- | ----- |
| BOOT | white |
| STANDBY | green |
| DETUMBLE | blue |
| POINTING | cyan |
| DOWNLINK | magenta |
| SAFE | red |

Bead 2 carries the uplink in three states: **blinking amber** before any command has ever arrived, **green** once the ground is in contact, **red** once the link has been silent past its 5 s timeout. Amber is a real distinction rather than a hedge - an un-latched loss fault before first contact only means the dead-man timer has not expired yet, which is unknown, not healthy - and it blinks because it is a state the rig sits in for a whole session, where a slow blink reads as searching and a steady amber reads as something settled.

**The bead asks the link, not the fault's response** (corrected 2026-07-30). It used to read `response_active(COMMAND_LINK_LOSS)`, which is false while the fault is inhibited - and the bench build inhibits it. The effect was that the bead went green on the first command ever received and stayed green for the rest of the session however long the ground had been gone. Suppressing a response is a decision about safing and says nothing about the link, so the display asks `link_lost()` directly. There is deliberately no fourth state for "silent but still inside the timeout": nothing keeps the link alive between typed commands, so that would be the normal condition during ordinary bench use and would blink continuously.

The whole array runs at level 4 of 255. The beads sit behind printed plastic and bleed through the walls at anything brighter, and 4 is the floor: the mixed colors set one channel to a fraction of it, and below that there are not enough steps left to hold the hue.

### Fault bead ladder

Bead 1 shows the worst rung that applies, highest first:

| Rung | Color | Meaning |
| ---- | ----- | ------- |
| Critical | red | a latched Critical fault whose response is live - the vehicle is safing or has safed |
| Degraded | orange | a latched Degraded fault - a capability was lost and a documented fallback is in force |
| Warning | yellow | a latched Warning fault - off-nominal, reported only |
| Inhibited | blue | something is latched but every latched fault has its response inhibited (REQ-FAULT-012) |
| Clean | off | nothing latched |

An inhibited fault is deliberately excluded from the three alarm rungs and collected into the one below them. It remains in the heartbeat and in the boot banner, so nothing is hidden - but a bench build routinely runs with subsystems absent, and letting those hold the bead red on every boot is how a red indicator stops carrying information. Blue sits outside the red/orange/yellow alarm family on purpose: it reads as status, not as a call to act.

## Attitude control (ADCS)

**REQ-ADCS-001** - In DETUMBLE the flight software shall command the reaction wheel to reduce the measured body rate below a defined threshold.  
**Type**: Functional  
**Status**: **working on the rig (2026-08-04)** - a hand-spun platform pulls itself into DETUMBLE, the rate collapses to the deadband, and the vehicle steps back out on its own, repeatably and over the radio. Torques sit at 8-9 mN m against the 50 mN m ceiling. SIL-011 nulls a 2 rad/s spin in the model. Proportional rate feedback, no integral: against stiction an integral winds up while the platform is stuck and overshoots when it breaks loose.

The gain is the rig's, not the model's. At the modelled 0.02 N m per rad/s the loop commanded more torque than the bearing's breakaway and kicked the platform loose in alternating directions - a stick-slip limit cycle at ~90 deg/s that the model never showed, because the model's breakaway friction was a guess. 0.008 is what the bench accepts.

**The controller does about two and a half times what friction does (A/B, 2026-08-04).** "It stopped" is not proof on a platform that stops itself, so the same spin was run twice at 10 Hz on the wired link: in SAFE, where no torque is commanded and autonomous entry is disabled, and in STANDBY, where the spin pulls the vehicle into DETUMBLE. Both runs are checked in under `docs/data/` and graded by `tools/overlay.py`, which measures the arrest - the last unbroken run of falling rate - rather than the whole window, because a hand-spun run has a plateau in it while the platform is still being pushed and averaging that in halves the answer.

Friction alone shed the rate at **161 deg/s^2**; with the controller, **412 deg/s^2**. The peak commanded torque was 13 mN m, which against the measured platform inertia predicts ~164 deg/s^2 of additional deceleration against the ~250 observed - consistent within what an unmeasured `kOhmsPerKt` can account for, and pointing the same way as every other estimate of it. The measurement needs the control-rate telemetry: at the heartbeat's 1 Hz the whole decay is a single sample.  
**Verification**: SIL (single-axis plant model) and HIL (reaction-wheel rig)  
**Artifact**: fsw/src/attitude_control.cpp, fsw/test/test_attitude_control.cpp, fsw/sil/plant.cpp, fsw/sil/scenarios/sil_011_detumble.yaml, docs/reports/sil/SIL-011.md

**REQ-ADCS-002** - In POINTING the flight software shall hold a commanded single-axis attitude within a defined error band.  
**Type**: Functional  
**Status**: **working on the rig for small slews (2026-08-04)** - commanded to a bearing 28 degrees away, untethered and over the radio, the platform converged monotonically and settled at **0.7 degrees of error against a 2.9 degree band**, then held. No overshoot.

Two things about how it gets there, both properties of this plant rather than of the software:

**It is a cascade, not a PD on angle.** The original law asked for torque proportional to heading error, and at `k_angle = 0.4` a 7 degree error already demanded the wheel's entire ceiling - so POINTING was a relay, not a controller, and the rig answered with 130 deg/s slams and 19 degrees of overshoot. The outer loop now turns heading error into a *slew rate* bounded well inside the wheel's momentum budget, and the inner loop is REQ-ADCS-001's law with that rate as its setpoint. Detumble is the same equation with a setpoint of zero, which is why both modes share one sign convention and one measured gain.

**It inches rather than sweeping, and that is correct here.** At the stable gain the feedback asks for ~2 mN m and the bearing does not let go until several times that, so a fixed feedforward supplies the shove, applied only while the platform is stopped. Break loose, move, push drops out, friction stops it, repeat. Smooth rotation would need sustained torque above kinetic friction while staying under the gain that oscillates, and on this bearing those two windows do not overlap. A vehicle in free fall has no bearing and no stiction; the stepping is an artifact of testing attitude control on a lazy susan, and it is the honest limit of the rig rather than of the flight software.

The feedforward is now sized against a measurement rather than a bound: **breakaway is 5 mN m**, taken 2026-08-04 by the vehicle pulsing its own wheel at rising torque over the radio with nothing plugged into it (`PULSE_WHEEL`, and the `breakaway` console macro that drives it). The 8 mN m feedforward clears it by 1.6x. **Owed**: larger slews are untested and are expected to run the wheel out of momentum, which is REQ-ADCS-003's territory.  
**Verification**: SIL (single-axis plant model) and HIL (reaction-wheel rig)  
**Artifact**: fsw/src/attitude_control.cpp, fsw/test/test_attitude_control.cpp

**REQ-ADCS-003** - Actuator commands shall be saturation-limited, and sustained saturation shall raise a dedicated actuator fault added with the actuator-control path.  
**Type**: Functional  
**Status**: SIL-verified, both halves (SIL-012, 9/9). The limit half: the controller clamps and reports that it clamped, and SIL-011 stays inside the wheel's rate limit. The fault half: `WHEEL_SATURATED` latches at 90% of the wheel's measured top speed after a second of debounce, retreats the steering modes to STANDBY as a Degraded fallback, and STANDBY unwinds the wheel.

**The rig is what made this concrete.** Commanded 90 degrees under the old relay law, the platform travelled about 28 and stopped dead, holding full commanded torque with the rate at zero for fifteen seconds - the wheel at its speed limit, delivering nothing, and no telemetry saying so. The budget explains it: `j_wheel * wheel_rate_max` is 3.9e-3 kg m^2/s, which against the platform is 0.87 rad/s of body rate in total, and any sustained push exceeds that in well under a second.

**The recovery is the interesting half.** A wheel that cannot be desaturated has no cure, and a fault with no responder is a catalog entry - so the fault landed with momentum dumping rather than before it. A real spacecraft dumps against magnetorquers or thrusters; this one dumps against its own bearing. Any torque under the platform's breakaway spins the wheel down without moving the body at all, so the stiction that makes pointing hard on this rig is exactly what makes the wheel rechargeable. Dumping runs in STANDBY only - DETUMBLE and POINTING are steering and must not have their actuator drained underneath them - which is also why the fallback retreats *to* STANDBY: the retreat is where the cure lives, not a parking space. The fault stays latched for the ground to clear (REQ-FAULT-010), because a vehicle that silently re-arms hides how often it ran out.

**Owed: HIL.** The dump is verified against the plant model, not the rig, and it rests on the platform's breakaway being above the dump torque - measured at 5 mN m commanded against a 3 mN m dump.  
**Verification**: unit test and HIL  
**Artifact**: fsw/src/attitude_control.cpp, fsw/test/test_attitude_control.cpp, fsw/sil/scenarios/sil_012_pointing_saturation.yaml, docs/reports/sil/SIL-012.md

## Payload

**REQ-ADCS-004** - The ground shall be able to command the bearing held in POINTING, and the flight software shall report the held bearing, the commanded bearing, and the measured rate as telemetry.  
**Type**: Functional  
**Status**: **demonstrated over the radio (2026-08-04)** - `SET_HEADING 30` was commanded from the ground station with no cable on the vehicle, and the attitude telemetry showed the target, the heading closing on it, and the error settling inside the band. `SET_HEADING` carries a binary angle - one byte spanning a full turn, 1.4 degrees a step, which is why a commanded 30 reads back as 29.5 - and `attitude_status_t` reports heading, target, rate and commanded torque. It now goes out every control cycle rather than at the heartbeat's rate, because a 10 Hz loop cannot be judged from 1 Hz samples; the beacon decimates it back to once a second so the air-time budget is unchanged. The bearing is relative to POINTING entry, which is the only frame this vehicle can honestly offer while the wheel's magnets hold the magnetometer off.  
**Verification**: unit test and HIL  
**Artifact**: fsw/test/test_comms.cpp, fsw/src/executive.cpp

**REQ-PAY-001** - An accepted CAPTURE_IMAGE command shall start a payload image capture without blocking the control cycle; the capture outcome shall be reported in telemetry rather than returned to the command.  
**Type**: Functional  
**Status**: bench-verified (CAPTURE_IMAGE accepted in POINTING, the frame taken without a missed control cycle, and the result downlinked intact)  
**Verification**: unit test and HIL  
**Artifact**: fsw/src/executive.cpp, obc/Src/devices/ov2640.c, fsw/test/test_executive.cpp

**REQ-PAY-002** - Loss of the payload camera shall raise CAMERA_DROPOUT; the payload is not required for vehicle safety, so the fault shall latch and report without changing mode.  
**Type**: Functional  
**Status**: bench-verified (SPI3 harness pulled on a live board: CAMERA_DROPOUT latched within one heartbeat, the vehicle held STANDBY, reconnect and reset came up clean. The harness is shared, so this shows the fault path rather than isolating the camera)  
**Verification**: unit test and HIL  
**Artifact**: fsw/src/sensor_monitor.cpp, fsw/test/test_sensor_monitor.cpp, docs/wiring.md (the ungrounded-camera mechanism found by this test)

**REQ-PAY-003** - A captured frame shall be held in the camera's own buffer and read out in caller-sized chunks, so that no image-sized buffer is allocated in flight-software RAM.  
**Type**: Constraint  
**Status**: bench-verified (a 7299-byte frame drained to exactly its reported length, FF D8 to FF D9, through a 64-byte stack buffer). The per-chunk burst close is verified by use rather than by a dedicated re-test: the 2026-07-30 over-the-air images reassembled with intact markers through per-chunk closes, rewinds, and selective-repeat sweeps of the same fifo, which is the read-pointer-survives-deselect evidence that was owed  
**Verification**: inspection and HIL  
**Artifact**: obc/Src/devices/ov2640.c

**REQ-PAY-004** - In DOWNLINK the flight software shall direct the payload buffer to be emptied to the ground, and the platform shall do so in chunks at a rate bounded by the link and without blocking the control cycle. Each chunk shall identify its image, its index, and the total count, so that a contact pass ending mid-image leaves a resumable set of chunks rather than an unusable partial stream.  
**Type**: Functional  
**Status**: bench-verified over the air (2026-07-30: an 800x600 capture reassembled to a 22240-byte file with intact markers). The fsw signals whether to downlink and the platform paces it - the chunks-per-cycle bound was a link property sitting on the wrong side of the PAL. Lost chunks are recovered by selective repeat: the image goes out once, the ground names its gaps over the LoRa uplink (`chunk_request_t`), and the vehicle resends exactly those from the frame still held in the camera's fifo  
**Verification**: unit test and HIL  
**Artifact**: fsw/src/executive.cpp, fsw/test/test_executive.cpp, obc/Src/freertos/downlink_task.cpp, tools/ground/payload.py, tools/tests/test_payload.py

The per-cycle bound is a real-time budget rather than a preference: the UART's transmit ring is 256 bytes and telemetry already spends about 80 of it each cycle, so a larger burst would block in `uart_write` waiting for the ring to drain and stall the control loop. The image length sent is the JPEG length found by scanning for the end-of-image marker, not the camera FIFO's reported length - those differ by a few hundred bytes of padding, which would otherwise be transmitted during a contact pass.

**REQ-PAY-006** - The ground shall be able to command a multi-frame survey - a sequence of point, capture and downlink at different bearings - and assemble the result into a single image.
**Type**: Functional
**Status**: in progress - **the sequence flies, the image is not worth having yet (2026-08-04)**. One typed word (`survey 4 60`) flew three legs unattended on battery over the radio: point, capture, downlink with selective repeat, park in STANDBY to unwind the wheel, slew, repeat. That half works, and the sequencer is unit-tested.

What does not work is the part that makes it a panorama. **The bearings land short of the commanded ones** - roughly 0, 9 and 28 degrees against a commanded 0, 20, 40 - because a slew that size runs the wheel out of momentum before it arrives (REQ-ADCS-003). Frames a few degrees apart overlap almost entirely, so the assembled strip is three nearly identical pictures rather than a swept scene. The macro is honest about it at the time (`aim timed out - shooting where it points`), and no output is checked in, because a result that looks like a panorama in a filename and not in the file is worse than none.

**It is a ground macro, not a SURVEY mode**, which is what the roadmap first sketched. A mode is a posture the vehicle holds; a survey is a sequence of postures it already has, so a SURVEY mode would have behaved exactly like POINTING except for who was counting - the case `roadmap.md`'s own design rule warns against. The sequencing lives on the ground where it is visible and abortable, the same reasoning the `shoot` macro settled first.

**Owed**: bearing separation the frames can actually use, which is an actuator problem rather than a sequencing one - more wheel momentum (the flywheel has 8 of 16 weight pockets empty) or less bearing friction. The stitcher is a plain side-by-side join with no feature matching, which is the right shape once the frames are far enough apart to need it.
**Verification**: unit tests for the sequencer; HIL for the assembled result, once the slews land
**Artifact**: tools/ground/session.py, tools/tests/test_session.py, tools/stitch.py

**REQ-PAY-005** - The reported size of a captured image shall be the size of the image data, determined by locating its end-of-image marker, and not the raw fill level of the camera's buffer.  
**Type**: Functional  
**Status**: bench-verified (two captures of different scenes both reported a 7688-byte FIFO while their images measured 7265 and 7362 bytes)  
**Verification**: unit test and HIL  
**Artifact**: obc/Src/devices/ov2640.c

## Platform abstraction and portability

**REQ-PAL-001** - The flight software shall reach active platform services - time and outbound link I/O - only through the platform abstraction layer, and shall contain no register or peripheral access. Inbound commands and sensor samples shall enter as cycle inputs assembled at the platform boundary.
**Type**: Constraint  
**Status**: inspection-verified, automated (`tools/tests/test_pal_boundary.py` in `just test`: files under `fsw/src` and `fsw/include` may include only the standard library, `etl/`, `fsw/` and `protocol/`, and may not name a peripheral outside a comment. Its negative cases found a hole in the check itself - the first pattern could not match `NVIC_EnableIRQ`, since `_` is a word character)  
**Verification**: inspection  
**Artifact**: fsw/include/fsw/platform.hpp, fsw/platform/host/platform_host.cpp, tools/tests/test_pal_boundary.py

**REQ-PAL-002** - The identical flight-software source shall build and run on the host (SIL) and cross-compile onto the STM32 (HIL and flight); only the platform backend shall differ.  
**Type**: Constraint  
**Status**: bench-verified (the unmodified flight-software library builds and runs on both targets; the STM32 image beacons its real mode and fault state)  
**Verification**: inspection and build (both targets compile the same sources)  
**Artifact**: backend chosen per executable (host backend for the unit tests, SIL backend in fsw/sil/sil_shim.cpp, STM32 backend in fsw/platform/stm32/platform_stm32.cpp); docs/reports/hil/HIL-001.md

## Verification and traceability

**REQ-VV-001** - The SIL harness shall drive the flight software from a declared scenario - an initial state, an input and fault timeline, and the expected response - and shall emit a pass/fail report. The runner shall be scenario-agnostic: any scenario it can express runs the same way, with no fault-specific logic in the runner.  
**Type**: Functional  
**Status**: SIL-verified (two scenarios of different kinds run through the unmodified runner)  
**Verification**: demonstration (a scenario produces a report)  
**Artifact**: fsw/sil/sil_shim.cpp, tools/sil_runner.py, docs/reports/sil/SIL-001.md, docs/reports/sil/SIL-002.md

**REQ-VV-002** - Every SIL and HIL scenario shall have an id and shall trace to the requirement(s) it verifies; the report shall record the scenario id, the requirement id, and the observed versus expected result.  
**Type**: Constraint  
**Status**: SIL-verified  
**Verification**: inspection  
**Artifact**: fsw/sil/scenarios/, fsw/hil/scenarios/, docs/reports/, docs/verification.md

**REQ-VV-003** - The HIL harness shall consume live STM32 telemetry, detect link loss against a heartbeat timeout, and capture packet timing.  
**Type**: Functional  
**Status**: HIL-verified (loss declared 5.03 s after the last live heartbeat and recovery observed; packet timing measured on the scope)  
**Verification**: HIL (timing capture + timeout demonstration)  
**Artifact**: tools/hil_runner.py, docs/reports/hil/HIL-001.md, docs/reports/hil/HIL-002.md, docs/reports/hil/HIL-001-scope.md

**REQ-VV-004** - Every requirement shall trace forward to at least one verifying artifact, and code and tests shall trace back to a requirement id; the trace shall be kept current as the system evolves.  
**Type**: Constraint  
**Status**: unit-verified (checked automatically both ways on every test run, so the trace cannot rot silently between reviews)  
**Verification**: automated check  
**Artifact**: tools/traceability.py, tools/tests/test_traceability.py, plus the REQ ids carried in code, tests, and the transition log

The check runs in two directions because they catch different decay. Forward: a requirement whose status claims verification must name an artifact, and every path it names must exist - this catches evidence deleted, moved, or renamed out from under a claim. Backward: every REQ id cited anywhere in the tree must exist in this document - this catches typos and ids left behind when a requirement is renumbered or dropped. Artifacts that are prose rather than a file are reported as unchecked rather than passed, so the report states what it could not verify instead of implying it did.
