# Requirements

Running requirements for the avionics stack. Each requirement has an ID, a statement, a status, and a verification method (test, analysis, inspection, or demonstration); once there is evidence, the artifact is listed too. Requirements are written ahead of the code they govern, so many start as planned until their phase lands. The status, not the existence of the requirement, tracks reality: planned -> in progress -> unit-verified -> SIL-verified -> bench-verified -> HIL-verified. A requirement whose verification line lists methods beyond its current status still owes that evidence.

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
| STANDBY | DETUMBLE, SAFE |
| DETUMBLE | STANDBY, POINTING, SAFE |
| POINTING | STANDBY, DOWNLINK, SAFE |
| DOWNLINK | STANDBY, POINTING, SAFE |
| SAFE | none autonomously (ground-commanded to STANDBY only - see REQ-MODE-006) |

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
**Status**: SIL-verified  
**Verification**: unit test, then SIL scenario (undervoltage -> SAFE, phase 3)  
**Artifact**: fsw/test/test_fault_manager.cpp, docs/reports/sil/SIL-001.md (scenario SIL-001)

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
**Status**: in progress (no-SAFE, latch, and report are SIL-verified; the ACCEL_GYRO_DROPOUT and POWER_DROPOUT -> STANDBY retreats are implemented, but their active-mode scenarios are still owed)
**Verification**: unit test and SIL  
**Artifact**: fsw/src/executive.cpp, fsw/src/sensor_monitor.cpp, fsw/test/test_fault_manager.cpp, docs/reports/sil/SIL-003.md

### Degraded fallback behaviors

REQ-FAULT-005 requires a Degraded fault to switch to its documented fallback - listed below - rather than forcing SAFE. A Warning fault such as `MAG_DROPOUT` latches and reports without changing mode.

| Fault | Fallback when latched |
| ----- | --------------------- |
| ACCEL_GYRO_DROPOUT | In POINTING or DETUMBLE - the modes that depend on body-rate feedback - retreat to STANDBY, a stable idle; holding attitude without the gyro is unsafe, but the loss is not mission-ending. In any other mode, latch and report only. |
| POWER_DROPOUT | In POINTING, DETUMBLE, or DOWNLINK - the high-power modes - retreat to STANDBY; with the power monitor unreadable, running power-hungry operations blind to brownout/overcurrent is unsafe, and STANDBY's lower draw cuts the risk. In any other mode, latch and report only. |

The active retreats - POINTING/DETUMBLE -> STANDBY on ACCEL_GYRO_DROPOUT, and POINTING/DETUMBLE/DOWNLINK -> STANDBY on POWER_DROPOUT - run in the executive's fault-response step and are logged as `FaultEntry` transitions stamped REQ-FAULT-005. Recovery is ground-commanded like every fault (CLEAR_FAULT, REQ-FAULT-010); the planned RESET_DEVICE command will let the ground re-initialize the affected sensor before clearing it.

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

## Executive

**REQ-EXEC-001** - The flight software shall process each control cycle's inputs in a fixed, documented order: fault-sample ingestion, command validation, fault response, command dispatch. The fault response shall take precedence: a command accepted in the same cycle a critical fault forces SAFE shall not override the SAFE entry. Command acceptance acknowledges validation only; execution outcome is observed through telemetry.  
**Type**: Functional  
**Status**: SIL-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/src/executive.cpp, fsw/test/test_executive.cpp, docs/reports/sil/SIL-008.md

## Comms - command uplink and telemetry downlink

**REQ-CMD-001** - The flight software shall validate every command - known id, in-range parameters, and legal in the current mode - before acting on it; an invalid command shall be rejected and reported, never executed.  
**Type**: Functional  
**Status**: SIL-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/test/test_comms.cpp, docs/reports/sil/SIL-002.md (scenario SIL-002)

**REQ-CMD-002** - Loss of ground command contact for longer than a defined timeout shall raise COMMAND_LINK_LOSS.  
**Type**: Functional  
**Status**: SIL-verified  
**Verification**: unit test and SIL  
**Artifact**: fsw/test/test_comms.cpp, docs/reports/sil/SIL-005.md

**REQ-CMD-003** - Every command shall be acknowledged in telemetry as accepted or rejected, with a reason given on rejection.  
**Type**: Functional  
**Status**: SIL-verified (both verdicts observed as decoded telemetry frames; HIL still owed)  
**Verification**: unit test (ack construction), SIL and HIL  
**Artifact**: fsw/test/test_comms.cpp, docs/reports/sil/SIL-002.md (scenario SIL-002)

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
**Status**: in progress  
**Verification**: inspection and HIL  
**Artifact**: dual UART (USART2 console, USART6 downlink); LoRa transport (phase 8)

## Real-time execution and recovery

**REQ-RT-001** - The flight software shall maintain a monotonic millisecond time base, used to timestamp every logged event.  
**Type**: Performance  
**Status**: bench-verified  
**Verification**: demonstration and inspection  
**Artifact**: bsp SysTick driver; millis() passed into each fsw cycle

**REQ-RT-002** - The decision paths shall run at a fixed, bounded rate and shall allocate no memory dynamically (fixed-capacity containers only).  
**Type**: Constraint  
**Status**: in progress  
**Verification**: inspection and analysis  
**Artifact**: no-heap rule, ETL containers; fixed-rate loop (phase 6)

**REQ-RT-003** - Under concurrent workloads the flight software shall run as prioritized tasks with the control loop at the highest priority, and shall report task health (liveness and stack high-water).  
**Type**: Performance  
**Status**: planned  
**Verification**: HIL (scheduler-smoke run)

**REQ-RT-004** - An unhandled processor fault exception (hard fault, memory-management, bus, or usage fault) shall be caught by a dedicated handler that captures the fault context and performs a controlled reset, rather than leaving the processor halted in a default infinite loop.  
**Type**: Functional  
**Status**: bench-verified (forced a fault on the bench with a `udf` undefined instruction; the handler dumped pc=0x0800320c, CFSR=0x00010000 (UFSR.UNDEFINSTR) and HFSR=0x40000000 (FORCED - escalated to HardFault), then rebooted via NVIC_SystemReset, the next boot reading reset=software)  
**Verification**: demonstration (trigger a fault, observe the capture then reset)  
**Artifact**: bsp/Src/drivers/fault.c

**REQ-WDG-001** - An independent hardware watchdog shall reset the on-board computer if the flight software stops servicing it within the watchdog window.  
**Type**: Functional  
**Status**: planned  
**Verification**: HIL (demonstrated reset)

**REQ-WDG-002** - After any reset the flight software shall report the reset cause - including a watchdog reset - in a boot telemetry packet.  
**Type**: Functional  
**Status**: in progress (the reset-cause read and console boot-banner report are bench-demonstrated - a pin reset read back live as `BOOT: reset=pin`; the power-on / brownout / software / watchdog causes share the same decode path. Carrying it in the framed boot telemetry packet and demonstrating the watchdog-reset cause remain owed with phase 6)  
**Verification**: HIL  
**Artifact**: bsp/Src/drivers/reset.c, bsp/Inc/drivers/reset.h, bsp/Src/main.cpp

## Sensors and data validity

**REQ-SNS-001** - Every sensor sample shall be tagged with an acquisition timestamp and a validity flag; the flight software shall never use a sample marked invalid.  
**Type**: Functional
**Status**: bench-verified (the IMU sample carries an acquisition timestamp and per-half validity flags, streaming live on the bench; the sensor monitor treats an invalid half as a dropout rather than consuming it - a full attitude consumer that honors the flag lands with ADCS)
**Verification**: unit test and HIL
**Artifact**: common/protocol/msg.hpp (imu_data_t), bsp/Src/devices/icm20948.c, fsw/src/sensor_monitor.cpp, fsw/test/test_sensor_monitor.cpp

**REQ-SNS-002** - A sensor whose data is invalid, missing, or frozen beyond a defined staleness window shall raise that sensor's dropout fault.
**Type**: Functional
**Status**: unit-verified (the sensor monitor raises the matching dropout on invalid or frozen IMU sources, and raises POWER_DROPOUT when the INA228 power monitor sample is invalid; both invalid-source paths are bench-exercised - pulling the IMU latches ACCEL_GYRO_DROPOUT + MAG_DROPOUT, and POWER_DROPOUT latched while the INA228 was unreachable and cleared once it answered - with HIL still owed)
**Verification**: unit test and HIL
**Artifact**: fsw/src/sensor_monitor.cpp, fsw/test/test_sensor_monitor.cpp

**REQ-SNS-003** - Where redundant sources exist, disagreement beyond a defined threshold shall raise a dedicated disagreement fault added with that redundant sensor path.  
**Type**: Functional  
**Status**: planned  
**Verification**: unit test and SIL

**REQ-SNS-004** - A valid monitored power reading outside its configured operating limits shall raise the corresponding power fault (undervoltage, overvoltage, overcurrent).
**Type**: Functional
**Status**: unit-verified (the sensor monitor raises each power fault when its INA228 reading crosses the configured limit, after debounce; on the bench a live INA228 reading below the undervoltage limit latched UNDERVOLTAGE, with overvoltage/overcurrent and HIL still owed)
**Verification**: unit test and HIL
**Artifact**: fsw/src/sensor_monitor.cpp, fsw/test/test_sensor_monitor.cpp

**REQ-SNS-005** - A valid monitored temperature reading outside its configured operating limits shall raise the corresponding temperature fault (overtemperature, undertemperature).
**Type**: Functional
**Status**: unit-verified (the sensor monitor latches OVERTEMPERATURE/UNDERTEMPERATURE when a valid reading crosses the configured limit after debounce, and TEMP_DROPOUT on an invalid sample; overtemperature is Critical -> SAFE, undertemperature a report-only Warning since dropping to a low-power state would only cool the spacecraft further. The TMP117 streams valid temperature on the bench; a real limit crossing - warming the sensor past the threshold - is still owed on hardware)
**Verification**: unit test and HIL
**Artifact**: fsw/src/sensor_monitor.cpp, fsw/test/test_sensor_monitor.cpp

## Attitude control (ADCS)

**REQ-ADCS-001** - In DETUMBLE the flight software shall command the reaction wheel to reduce the measured body rate below a defined threshold.  
**Type**: Functional  
**Status**: planned  
**Verification**: SIL (NASA 42) and HIL (reaction-wheel rig)

**REQ-ADCS-002** - In POINTING the flight software shall hold a commanded single-axis attitude within a defined error band.  
**Type**: Functional  
**Status**: planned  
**Verification**: SIL (NASA 42) and HIL (reaction-wheel rig)

**REQ-ADCS-003** - Actuator commands shall be saturation-limited, and sustained saturation shall raise a dedicated actuator fault added with the actuator-control path.  
**Type**: Functional  
**Status**: planned  
**Verification**: unit test and HIL

## Platform abstraction and portability

**REQ-PAL-001** - The flight software shall reach active platform services - time and outbound link I/O - only through the platform abstraction layer, and shall contain no register or peripheral access. Inbound commands and sensor samples shall enter as cycle inputs assembled at the platform boundary.
**Type**: Constraint  
**Status**: in progress  
**Verification**: inspection  
**Artifact**: fsw/include/fsw/platform.hpp + host backend

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
**Artifact**: fsw/sil/scenarios/, fsw/hil/scenarios/, docs/reports/, docs/scenarios.md

**REQ-VV-003** - The HIL harness shall consume live STM32 telemetry, detect link loss against a heartbeat timeout, and capture packet timing.  
**Type**: Functional  
**Status**: HIL-verified (loss declared 5.03 s after the last live heartbeat and recovery observed; packet timing measured on the scope)  
**Verification**: HIL (timing capture + timeout demonstration)  
**Artifact**: tools/hil_runner.py, docs/reports/hil/HIL-001.md, docs/reports/hil/HIL-002.md, docs/reports/hil/HIL-001-scope.md

**REQ-VV-004** - Every requirement shall trace forward to at least one verifying artifact, and code and tests shall trace back to a requirement id; the trace shall be kept current as the system evolves.  
**Type**: Constraint  
**Status**: in progress  
**Verification**: inspection  
**Artifact**: this document, plus the REQ ids carried in code, tests, and the transition log
