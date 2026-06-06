# Requirements

Verification spine for the flight software. Each requirement has an ID, a statement, a verification method (test, analysis, inspection, or demonstration), and the artifact that proves it. Status follows the project ladder: planned, in progress, working in SIL, working on bench, working in HIL.

## Mode management

REQ-MODE-001 - The flight software represents the spacecraft operating mode as exactly one of BOOT, STANDBY, DETUMBLE, POINTING, DOWNLINK, or SAFE.
Verification: unit test. Artifact: fsw/test/test_mode_manager.cpp. Status: in progress.

REQ-MODE-002 - Every mode transition is logged with the timestamp, the trigger, the previous mode, the new mode, and the requirement ID it satisfies.
Verification: unit test. Artifact: fsw/test/test_mode_manager.cpp. Status: in progress.

## Fault management

REQ-FAULT-001 - The fault manager latches an active fault until it is explicitly cleared, and exposes the active set as the bitmask carried in heartbeat telemetry.
Verification: unit test. Artifact: fsw/test/test_fault_manager.cpp. Status: in progress.

REQ-FAULT-002 - An unresolved critical fault (undervoltage first) commands SAFE unless a documented degraded behavior exists, and only after a debounce window so a transient sample cannot latch it.
Verification: unit test now, SIL scenario in the first SIL slice. Artifact: fsw/test/test_fault_manager.cpp. Status: planned.
