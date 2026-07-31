# SmallSat Avionics - Build Plan

What's left to build, in order. The record of what's already done and how it went lives in `journal.md`; this file is forward-only. Depth-first: a few slices done with real proof beat many half-finished ones. Every working session, update the `Status -> Next action` block here and add a `journal.md` entry.

The companion documents: `journal.md` (the done-log), `hardware.md` (the physical build, placement rules, and tools), `wiring.md` (the bench wiring sheet). All were kept in an untracked folder outside the repo until 2026-07-29 and now live here under version control.

---

## Status -> Next action

Where the build actually is. History belongs in `journal.md`.

**Hardware:** all three plates built and wired (2026-07-26). The ESC is physically out of the
stack pending a replacement board, so `WHEEL_DROPOUT` and `COMMAND_LINK_LOSS` run under the
declared bench inhibits. Still owed: the PTC fuse before the LiPo ever runs.

**Software:** phases 0-6 done. Phase 7 has detumble SIL-verified and running autonomously on the
bench (auto-entry, auto-exit, resume); pointing holds an absolute compass heading, coarse indoors.
**Phase 8's deliverable is done:** the vehicle, with no data cable, was commanded over LoRa into
POINTING, captured, and downlinked an image over nRF24 that reassembled intact on the ground -
selective repeat refilled what the first pass dropped. Power still comes from the bench supply;
flying on the battery waits on the PTC fuse and the battery harness.

**Next, unblocked:**

1. Measure the bearing's breakaway torque. It is the last unmeasured plant parameter and it
   decides how fine the pointing can be on this rig.
2. Bench re-tests owed in `requirements.md`: the three power crossings on the 14.8 V bus and a
   temperature crossing (REQ-SNS-004/005).
3. A compass accuracy pass beyond the current smoothing, if indoor repeatability stays coarse -
   an ellipse fit is the next honest step.

**Blocked on the replacement ESC:** the wheel UART link, and HIL for every ADCS requirement. The
flight software for it is written and green on the host; what is missing is the wire. PA10 must
reach the ESC's TX (PB3 on J3) and PA9 its RX (PB4), crossed, with the ESC's USB out and a common
ground. Delete the `TEMP esc rx:` block in `control_task.cpp` once `ESC: up` appears.

## Phase ledger

| Phase | Title | Status |
| ----- | ----- | ------ |
| 0 | Repo + environment | done |
| 1 | Bare-metal firmware foundation | done on bench |
| 2 | Host C++ flight software + unit tests | done |
| 3 | First SIL slice | done |
| 4 | First HIL slice | done |
| 5 | Sensors (real telemetry & faults) | done on bench - all four sensors answering, and an image captured and downlinked end to end |
| 6 | Real-time task model + recovery (FreeRTOS + watchdog) | **done, HIL-verified 2026-07-29** - HIL-003 passed 7/7: all four tasks in all 60 reports, watchdog fed in every one, worst stack margin 104 words against a 64 floor, and 59 heartbeat periods at a 1.000 s mean. The bite was demonstrated separately (`reset=iwdg-watchdog`) |
| 7 | ADCS: closed-loop attitude control | control laws SIL-verified against the plant; autonomous detumble entry/exit/resume and the compass-anchored heading run on the bench. Blocked on the replacement ESC for the physical loop and its HIL |
| 8 | Capstone: untethered rig + demo | **deliverable done 2026-07-30** - commanded, imaged, and downlinked over radio with no data cable. Battery integration (with the PTC fuse) and the survey demo remain |

---

## Carried work

Items that outlived the phase they were opened in, or that are waiting on a part.

- **Secure boot + VTOR** - a bootloader at flash base verifies the app image, relocates
  `SCB->VTOR`, and jumps to it; A/B slots and rollback for safe updates. A design sketch, not
  scheduled. The rest of the boot-hardening pass (reset cause, fault handler, clock tree,
  watchdog, hard-float) is done and bench-verified.

- **PTC resettable fuse (Littelfuse RUEF300 or equivalent) - ordered, not yet arrived.** The main + lead currently runs through a temporary jumper. **This must be fitted before the LiPo is ever connected**; until then the bench PSU's current limit is the only fault protection. The one outstanding hardware item on the satellite.
- **Mechanical work on the gimbal plate:** lazy-susan stiction, flywheel inertia (fill pockets outermost-first, consider a larger radius), and tether routing up the spin axis. See the flywheel notes in `hardware.md`.
- **"Link never acquired" vs "link lost" in telemetry (phase 8, open design question).** `link_lost()` measures against `last_command_ms_ = 0`, so COMMAND_LINK_LOSS (Critical, debounce 1) latches ~5 s after every boot with no ground station and drops the rig to SAFE. That *response* is right - a spacecraft that cannot hear the ground should safe - and it is why bench demos need NOOP keep-alives. What is missing is **observability**: nothing distinguishes "never acquired" from "lost after contact". The status array already separates them (amber vs red, derived from an empty command log), but a heartbeat or log reader cannot. Worth carrying in telemetry once the ground station exists to read it; deliberately not a new fault or mode now, since the derivation already exists and there would be exactly one consumer.

---

## Design reference

Modes and faults are defined in `common/protocol/state.hpp` and specified in `requirements.md`.
Architecture decisions and their reasoning are in `architecture.md`. Two rules that decide scope
and live nowhere else:

- **A mode is a posture, not a parameter.** Something earns a mode when it changes what the
  vehicle is *doing*, not when it changes what the vehicle is *deciding with*. The cost of
  getting this wrong is two modes that behave identically appearing in every fallback list.
- **Build the final design, not a worse stopgap.** The exception is an optimisation gated on
  infrastructure that does not exist yet.

---

## Remaining phases

### Phase 7 - ADCS: closed-loop attitude control

**Goal:** closed-loop single-axis attitude control, shown **both** in the plant model **and** on the physical reaction-wheel rig, the two overlaid. The control loop is the highest-priority FreeRTOS task from Phase 6.
**Learn:** rigid-body attitude basics (angular momentum, reaction-wheel torque), a simple PD/PID, numerical integration of the plant, basic sensor fusion.
**Tools:** the in-repo plant model for the physics; the oscilloscope for the motor-drive PWM and phases; the printed reaction-wheel rig.
**Steps:**
1. **Sim first** - a single-axis body + reaction wheel + gyro as a plant model the SIL harness steps; run the control law against it on the host; plot the rate collapsing and the attitude settling.
2. **Then the rig** - the same control law on the STM32 driving the gimbal motor through the B-G431B-ESC1 + flywheel, IMU on the platform, free to rotate on the low-friction mount. Tumble it, the wheel detumbles it and holds a commanded heading.
3. **Overlay** - log both, plot sim against rig for the same controller.
4. **The camera becomes a second rate sensor (decided 2026-07-29).** A small raw frame (~80x60 grayscale or RGB565, a second OV2640 configuration alongside the JPEG one), a horizontal band collapsed to a 1-D intensity profile, cross-correlated against the previous frame: the peak shift is angular displacement in pixels, and over the frame interval that is **deg/s**. Roughly 3200 multiply-accumulates over +/-20 shifts on an 80-wide profile - nothing for a 180 MHz M4F with an FPU, comfortably 10 Hz. Sub-pixel by parabolic interpolation on the correlation peak.
   **This is what finally makes REQ-SNS-003 real.** That requirement says "where redundant sources exist, disagreement beyond a defined threshold shall raise a dedicated disagreement fault", and it has sat planned because the vehicle has no redundant sensor path. Camera-derived rate against gyro-derived rate is one, on the single axis the lazy susan actually has: two independent measurements of the same physical quantity, a threshold, and a fault.
   **Not a mode - a sensor.** It feeds `Inputs` alongside the IMU with its own validity flag.
   Two prerequisites and one bench constraint: the **payload task + SPI3 mutex** must land first (reading 80x60x2 = 9600 bytes at the measured ~500 KB/s costs ~20 ms, a fifth of the control cycle, and it must not spend that inside the control task); the raw register tables need vendoring the way the JPEG ones were; and **cross-correlation needs texture** - a blank white wall has no features and returns no peak, so the bench needs a poster or a cluttered background before anyone debugs "why is the rate always zero".
**Done when:** a clip and plots show the same controller detumbling and pointing in sim and on the rig, and the two rate sources tracking each other.

### Phase 8 - Capstone: what remains

The dual-link comms stack, the ground station, and the no-data-cable demo are done (see the
journal, 2026-07-30). What is left of the capstone:

1. **Battery integration.** Measure the assembled current draw (motor peaks + logic + both
   radios transmitting) and size the pack; fit the PTC fuse; run on battery, perturb the
   platform, watch it recover and downlink an image with no wires at all.
2. **The package.** The verification matrix, SIL/HIL reports, the sim-versus-rig overlay once
   the wheel is back, and a demo clip.

**The capstone imaging demo is a slew-and-image panorama, in a new SURVEY mode (decided 2026-07-29).** Command a controlled slew, capture at intervals through the rotation, downlink the set, and stitch it on the ground into one wide image. Slew-and-image is how a lot of smallsat imaging actually works, and it *uses* the attitude control rather than duplicating it, so almost all the new code is a ground-side stitcher. The result is one image that took attitude control, payload timing, and a multi-image downlink to produce.

**Why SURVEY is a mode when target-tracking is not.** The test: does it change what the vehicle is *doing*, or only what it is *deciding with*? A survey means deliberately slewing at a commanded rate, which directly contradicts POINTING's job of holding still - different control law, different risk, and a POWER_DROPOUT mid-survey should abort the slew rather than do what it does in POINTING. Target tracking is the same control law and the same actuator with the attitude error arriving from vision instead of an inertial setpoint, so it belongs as a **source selected by a command argument**, not a seventh mode. Two modes differing only in an input is exactly the duplication that lets someone narrow one fallback list and forget the other - the failure SIL-010 was written to catch. Adding SURVEY costs one `FSW_MODE_LIST` line (everything derives from the x-macro, and the drift tests catch the Python mirror), a row and column in `kAutoAllowed`, an LED bead case, the console ladder, a review of the three degraded-fallback lists, and its own scenarios.

**Autonomous capture triggering, if it is cheap by then.** Downsample, sum absolute differences against the last frame, and capture full-resolution only when the scene changed. Not a mode - a policy inside the payload path. Worth it for the line it earns: the spacecraft deciding which frame is worth the downlink budget, which is the honest answer to why this vehicle does not stream video.

**Done when:** the untethered demo is captured (recovery plus an image downlinked over the high-rate link), the verification matrix is complete, and the repo stands on its own.

---

## Simulation - the plant model (Phase 7)

A single-axis rigid body with one reaction wheel, written in-repo and driven by the existing SIL harness. **NASA 42 was evaluated and set aside (2026-07-30).**

**Why not 42.** It is a serious tool and its value is environment fidelity: IGRF geomagnetic field, gravity harmonics, drag, solar pressure, eclipse, plus sensor and actuator models and a 3D view. None of that describes a lazy susan on a bench. The rig is one wheel on one axis, and the dynamics that matter are conservation of angular momentum about that axis plus turntable friction - and **42 does not model the thing that will actually dominate the rig's behaviour**, which is stiction in a cheap bearing. That would have to be added to the plant model either way. Against that, the cost is real: 42 is developed on Linux and macOS and this bench is Windows, so it means MSYS2 or WSL; its input files are a format to learn; and its socket interface is a different shape from the current stdin-timeline shim, so it would be a second harness rather than an extension of the first. Days of work, most of it on build and configuration rather than on attitude control.

**What replaces it.** Platform inertia, wheel inertia, commanded torque, momentum exchange, and a tunable friction term. It runs inside the harness that already exists, and the control law - the interesting part - is identical either way. The visual is a plot of rate against time, which is what step 3's sim-versus-rig overlay needs anyway; a 3D render is decoration, a settling curve is evidence.

**The better validation, and the reason this is not a downgrade:** once the ESC returns, the plant model can be checked against the *real rig* - measured platform response to a commanded wheel torque. Agreement with hardware beats agreement between two simulations.

**Revisit if the scope changes.** 42 earns its complexity the moment the mission becomes orbital: detumble from a deployment tumble using magnetorquers against a modelled field, sun-pointing, eclipse-aware power. If that ever becomes the story, the small model stays as the bench-validation model and 42 joins alongside it.

**Set aside (on record):** Basilisk (CU Boulder - heavier Python astro framework, more than needed); cFS / NOS3 (NASA FSW framework / full sim - much bigger, aimed at a Linux flight computer, out of scope); OpenC3 COSMOS (not a sim - a telemetry ground-station dashboard, optional Phase 8 polish only).

---

## Resume mapping

What each phase unlocks on the hypothetical resume. Phases 1-6 are earned.

| Phase | Unlocks |
| ----- | ------- |
| 1, 4 | UART packet protocol; HIL testing; oscilloscope and logic analyzer |
| 2 | C++ mode/fault state machines; unit tests; FDIR |
| 3 | SIL fault injection; requirements and the V-model |
| 5 | sensor integration; IMU/SPI; power telemetry |
| 6 | FreeRTOS task model; watchdog validation |
| 7 | attitude control / reaction wheel; sensor fusion; plant modelling and numerical integration; Fusion 360 |
| 8 | LoRa wireless link; untethered demo; full V&V package |
