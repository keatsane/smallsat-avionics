# Requirements

Verification spine for the avionics stack. Each requirement has an ID, a statement, a status, and a verification method (test, analysis, inspection, or demonstration); once it has real evidence, the artifact that proves it is listed too. Requirements are written ahead of the code they govern, so most carry a planned status until their phase lands - the status, not the presence of a requirement, is what tracks reality. The status names the highest evidence level reached: planned -> in progress -> unit-verified -> SIL-verified -> bench-verified -> HIL-verified. A requirement whose verification line lists methods beyond its current status still owes that evidence.

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
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_mode_manager.cpp

**REQ-MODE-002** - On power-on or reset, the flight software shall initialize the current mode to BOOT before permitting any transition, and have an empty log.  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_mode_manager.cpp

**REQ-MODE-003** - The flight software shall permit only the autonomous mode transitions listed below. Any transition not listed - including a transition from a mode to itself - shall be treated as illegal.

| From | Legal transitions to |
| ---- | -------------------- |
| BOOT | STANDBY, DETUMBLE, SAFE |
| STANDBY | DETUMBLE, SAFE |
| DETUMBLE | STANDBY, POINTING, SAFE |
| POINTING | STANDBY, DOWNLINK, SAFE |
| DOWNLINK | STANDBY, POINTING, SAFE |
| SAFE | none autonomously (ground-commanded to STANDBY only - see REQ-MODE-006) |

**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_mode_manager.cpp

**REQ-MODE-004** - On a request for a transition that is not in the legal set, the flight software shall leave the current mode unchanged and shall not append a transition log entry.  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_mode_manager.cpp

**REQ-MODE-005** - Every operating mode (BOOT, STANDBY, DETUMBLE, POINTING, DOWNLINK) shall have a legal transition to SAFE, so a fault response can command SAFE from any operating mode.  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_mode_manager.cpp

**REQ-MODE-006** - The flight software shall perform no autonomous transition out of SAFE. The only permitted exit from SAFE shall be a ground-commanded transition to STANDBY.  
**Status**: unit-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/test/test_mode_manager.cpp

**REQ-MODE-007** - The flight software shall record, for every transition, the trigger that caused it, drawn from the set {PowerOn, Nominal, FaultEntry, FaultCleared, Timeout, Command}.  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_mode_manager.cpp

**REQ-MODE-008** - On every accepted transition, the flight software shall append one record to the mode transition log containing: the platform timestamp in milliseconds, the trigger, the previous mode, the new mode, and the requirement ID the transition serves.  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_mode_manager.cpp

**REQ-MODE-009** - The mode transition log shall be a fixed-capacity buffer that allocates no memory dynamically and retains at least the 32 most recent records; when full, the oldest record shall be overwritten.  
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

The fault catalog (14 faults: undervoltage, overcurrent, gyro-dropout, ...) is defined once in `common/protocol/state.hpp`; a fault's position there is both its id and its bit in the active-fault bitmask.

**REQ-FAULT-001** - The fault manager shall latch an active fault until it is explicitly cleared; a condition that clears on its own shall not un-latch the fault.  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_fault_manager.cpp

**REQ-FAULT-002** - A latched Critical fault shall command SAFE, and only after its debounce window so a transient sample cannot force it.  
**Status**: unit-verified  
**Verification**: unit test, then SIL scenario (undervoltage -> SAFE, phase 3)  
**Artifact**: fsw/test/test_fault_manager.cpp

**REQ-FAULT-003** - Each fault shall have a defined policy - severity, debounce threshold, and owning requirement - held in a single fault table that is the only place fault policy is written; a compile-time check shall keep that table sized to the fault catalog.  
**Status**: unit-verified  
**Verification**: inspection and unit test (compile-time size check)  
**Artifact**: fsw/src/fault_manager.cpp

**REQ-FAULT-004** - A fault shall latch only after its debounce threshold of consecutive bad samples is reached; a single transient bad sample shall never latch it.  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_fault_manager.cpp

**REQ-FAULT-005** - A fault of Warning or Degraded severity shall not by itself command SAFE; a Degraded fault shall switch to its documented fallback behavior, and every such fault shall be latched and reported in telemetry.  
**Status**: in progress  
**Verification**: unit test  
**Artifact**: fsw/test/test_fault_manager.cpp

**REQ-FAULT-006** - The flight software shall evaluate the full fault set once per control cycle and apply the required response; when more than one response is indicated, the most conservative one shall win (SAFE dominates).  
**Status**: unit-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/test/test_fault_manager.cpp

**REQ-FAULT-007** - The flight software shall define the fault catalog once, in common/protocol/state.hpp, as a single index enum; a fault's position in that list shall be both its id and its bit position in the active-fault bitmask.  
**Status**: unit-verified  
**Verification**: inspection  
**Artifact**: common/protocol/state.hpp

**REQ-FAULT-008** - The set of currently latched faults shall be exposed as a bitmask (bit n set when fault id n is latched); this bitmask is what the heartbeat carries (see REQ-TLM-002).  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_fault_manager.cpp

**REQ-FAULT-009** - A good (healthy) sample shall reset a fault's consecutive-bad-sample count, but shall not clear a fault that has already latched (see REQ-FAULT-001).  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_fault_manager.cpp

**REQ-FAULT-010** - A latched fault shall be cleared only by an explicit action - a ground command or a defined recovery sequence - never autonomously by the flight software.  
**Status**: unit-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/test/test_fault_manager.cpp, fsw/test/test_executive.cpp

**REQ-FAULT-011** - The fault manager shall maintain a time-ordered log of fault state-change events; each entry shall record the platform timestamp, the fault id, and whether the fault latched or cleared. Only edges shall be logged - never per-sample - and the log shall be a fixed-capacity ring that overwrites its oldest entry when full.  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_fault_manager.cpp

## Executive

**REQ-EXEC-001** - The flight software shall process each control cycle's inputs in a fixed, documented order: fault-sample ingestion, command validation, fault response, command dispatch. The fault response shall take precedence: a command accepted in the same cycle a critical fault forces SAFE shall not override the SAFE entry. Command acceptance acknowledges validation only; execution outcome is observed through telemetry.  
**Status**: unit-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/src/executive.cpp, fsw/test/test_executive.cpp

## Comms - command uplink and telemetry downlink

**REQ-CMD-001** - The flight software shall validate every command - known id, in-range parameters, and legal in the current mode - before acting on it; an invalid command shall be rejected and reported, never executed.  
**Status**: unit-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/test/test_comms.cpp

**REQ-CMD-002** - Loss of ground command contact for longer than a defined timeout shall raise COMMAND_LINK_LOSS.  
**Status**: unit-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/test/test_comms.cpp

**REQ-CMD-003** - Every command shall be acknowledged in telemetry as accepted or rejected, with a reason given on rejection.  
**Status**: unit-verified (ack construction; the emission is verified at SIL/HIL)  
**Verification**: unit test (ack construction), SIL and HIL  
**Artifact**: fsw/test/test_comms.cpp

**REQ-CMD-004** - Every handled command shall be appended to a command event log recording the platform timestamp, the command id, the verdict, and the rejection reason. The log shall be a fixed-capacity ring that allocates no memory dynamically and overwrites its oldest entry when full.  
**Status**: unit-verified  
**Verification**: unit test  
**Artifact**: fsw/test/test_comms.cpp

**REQ-TLM-001** - All telemetry shall be framed with a sync header, message id, length, and CRC-16; a corrupted or mis-framed packet shall be detected and discarded, never acted on.  
**Status**: unit-verified (the codec; the bench emission was retired when the protocol moved into the flight software, and returns through the executive)  
**Verification**: unit test and HIL  
**Artifact**: common/protocol/frame.cpp, fsw/test/test_comms.cpp, tools/uart_monitor.py

**REQ-TLM-002** - The flight software shall emit a periodic heartbeat carrying uptime, a sequence number, the current mode, and the active-fault bitmask.  
**Status**: unit-verified (wire layout, builder, and cadence; the emission is verified at SIL/HIL)  
**Verification**: unit test and HIL  
**Artifact**: fsw/test/test_comms.cpp

**REQ-TLM-003** - The heartbeat sequence number shall increment monotonically so the ground can detect dropped packets.  
**Status**: unit-verified  
**Verification**: unit test; demonstration (scope/host decode)  
**Artifact**: fsw/test/test_comms.cpp, tools/uart_monitor.py

**REQ-TLM-004** - The telemetry transport shall be swappable behind the frame format - UART now, radio later - with no change to the wire format.  
**Status**: in progress  
**Verification**: inspection and HIL  
**Artifact**: dual UART (USART2 console, USART6 downlink); LoRa transport (phase 8)

## Real-time execution and recovery

**REQ-RT-001** - The flight software shall maintain a monotonic millisecond time base, used to timestamp every logged event.  
**Status**: bench-verified  
**Verification**: demonstration and inspection  
**Artifact**: bsp SysTick driver; fsw platform now_ms()

**REQ-RT-002** - The decision paths shall run at a fixed, bounded rate and shall allocate no memory dynamically (fixed-capacity containers only).  
**Status**: in progress  
**Verification**: inspection and analysis  
**Artifact**: no-heap rule, ETL containers; fixed-rate loop (phase 6)

**REQ-RT-003** - Under concurrent workloads the flight software shall run as prioritized tasks with the control loop at the highest priority, and shall report task health (liveness and stack high-water).  
**Status**: planned  
**Verification**: HIL (scheduler-smoke run)

**REQ-WDG-001** - An independent hardware watchdog shall reset the on-board computer if the flight software stops servicing it within the watchdog window.  
**Status**: planned  
**Verification**: HIL (demonstrated reset)

**REQ-WDG-002** - After any reset the flight software shall report the reset cause - including a watchdog reset - in a boot telemetry packet.  
**Status**: planned  
**Verification**: HIL

## Sensors and data validity

**REQ-SNS-001** - Every sensor sample shall be tagged with an acquisition timestamp and a validity flag; the flight software shall never use a sample marked invalid.  
**Status**: planned  
**Verification**: unit test and HIL

**REQ-SNS-002** - A sensor whose data stops updating within a defined staleness window shall raise DATA_STALE and the matching dropout fault.  
**Status**: planned  
**Verification**: unit test and HIL

**REQ-SNS-003** - Where redundant sources exist, disagreement beyond a defined threshold shall raise SENSOR_DISAGREEMENT.  
**Status**: planned  
**Verification**: unit test and SIL

## Attitude control (ADCS)

**REQ-ADCS-001** - In DETUMBLE the flight software shall command the reaction wheel to reduce the measured body rate below a defined threshold.  
**Status**: planned  
**Verification**: SIL (NASA 42) and HIL (reaction-wheel rig)

**REQ-ADCS-002** - In POINTING the flight software shall hold a commanded single-axis attitude within a defined error band.  
**Status**: planned  
**Verification**: SIL (NASA 42) and HIL (reaction-wheel rig)

**REQ-ADCS-003** - Actuator commands shall be saturation-limited, and sustained saturation shall raise ACTUATOR_SATURATION.  
**Status**: planned  
**Verification**: unit test and HIL

## Platform abstraction and portability

**REQ-PAL-001** - The flight software shall reach the outside world - time, link, sensors - only through the platform abstraction layer, and shall contain no register or peripheral access.  
**Status**: in progress  
**Verification**: inspection  
**Artifact**: fsw/include/fsw/platform.hpp + host backend

**REQ-PAL-002** - The identical flight-software source shall build and run on the host (SIL) and cross-compile onto the STM32 (HIL and flight); only the platform backend shall differ.  
**Status**: in progress  
**Verification**: inspection and build (both targets compile the same sources)  
**Artifact**: backend chosen per executable (host backend for the unit tests, SIL backend in fsw/sil/sil_shim.cpp); integrated STM32 build (phase 4 to 5)

## Verification and traceability

**REQ-VV-001** - The SIL harness shall drive the flight software from a declared scenario - an initial state, an input and fault timeline, and the expected response - and shall emit a pass/fail report. The runner shall be scenario-agnostic: any scenario it can express runs the same way, with no fault-specific logic in the runner.  
**Status**: in progress  
**Verification**: demonstration (a scenario produces a report)

**REQ-VV-002** - Every SIL and HIL scenario shall have an id and shall trace to the requirement(s) it verifies; the report shall record the scenario id, the requirement id, and the observed versus expected result.  
**Status**: planned  
**Verification**: inspection

**REQ-VV-003** - The HIL harness shall consume live STM32 telemetry, detect link loss against a heartbeat timeout, and capture packet timing.  
**Status**: planned  
**Verification**: HIL (timing capture + timeout demonstration)

**REQ-VV-004** - Every requirement shall trace forward to at least one verifying artifact, and code and tests shall trace back to a requirement id; the trace shall be kept current as the system evolves.  
**Status**: in progress  
**Verification**: inspection  
**Artifact**: this document, plus the REQ ids carried in code, tests, and the transition log
