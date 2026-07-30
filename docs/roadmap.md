# SmallSat Avionics - Build Plan

What's left to build, in order. The record of what's already done and how it went lives in `journal.md`; this file is forward-only. Depth-first: a few slices done with real proof beat many half-finished ones. Every working session, update the `Status -> Next action` block here and add a `journal.md` entry.

The companion documents: `journal.md` (the done-log), `hardware.md` (the physical build, placement rules, and tools), `wiring.md` (the bench wiring sheet). All were kept in an untracked folder outside the repo until 2026-07-29 and now live here under version control.

---

## Status -> Next action

- **The satellite hardware build is complete (2026-07-26).** All three plates are wired, assembled, and verified as far as they can be without firmware. The gimbal plate runs closed-loop FOC with a verified AS5600 encoder and has demonstrated the reaction-wheel effect; the compute plate carries the OBC slice, the sensor board with all four devices answering, and the power distribution board feeding the stack from the 14.75 V bus; the comms plate carries the 3-bead WS2812 array and both radios on a shared SPI3 bus with a dedicated 3.3 V buck. Fault thresholds are sized for 4S LiPo (13.6 V under / 17.0 V over / 1.5 A overcurrent).
- **Phase 5 is complete and bench-proven (2026-07-29).** The whole payload arc ran on real silicon: `SET_MODE DETUMBLE -> POINTING -> CAPTURE_IMAGE -> SET_MODE DOWNLINK`, 121 chunks reassembled to a 6759-byte jpeg on disk with its markers intact at both ends. The ESC node is physically out of the stack pending a replacement board, so `WHEEL_DROPOUT` and `COMMAND_LINK_LOSS` run under the declared bench-inhibit list.
- **Phase 6 is closed (2026-07-29).** Kernel, task model, task-health telemetry, stacks sized from measured peaks, a watchdog that bites, and HIL-003 passing 7/7 as the graded evidence. REQ-RT-002, REQ-RT-003 and REQ-WDG-001 are HIL-verified; REQ-FAULT-005 is SIL-verified.
- **The docs are consolidated and edited (2026-07-29).** Eleven files down to nine, the planning documents folded in and cut against a boundary - architecture holds decisions and structure, hardware holds the physical objects and mechanical constraints, bom holds the parts, wiring holds the pin map. `README.md` has a documentation index and `requirements.md` a section table of contents. The pass caught a stale mode-bead color table, three undocumented scenarios, and a SIL-007 description that predated its own rewrite.
- **Next action: phase 7, gated on the replacement ESC board.** The wheel link's flight software is finished and green on the host; what is missing is the wire. Until the board arrives, the useful work is host-side and runs without the rig: the NASA 42 single-axis model and the control law itself.
- **A test-coverage audit ran 2026-07-29**, mapping all 60 requirements to their claimed verification: 21 unit, 15 SIL, 8 bench, 7 HIL, the rest planned or in progress. The one genuine SIL gap was REQ-FAULT-005's retreats, now closed by SIL-009 and SIL-010. Remaining owed evidence is hardware-gated (REQ-PAY-002 wants the camera unplugged on a live board; REQ-TLM-004 waits on the radios) or is phase 7's ADCS set.
- **Still owed from phase 5:** the ESC wire - the OBC sees zero bytes (`waiting=0 ore=0 fe=0 ne=0`), so PA10 must reach the ESC's TX (PB3 on J3) and PA9 its RX (PB4), crossed not straight, with the ESC's USB **out** (J3 and its ST-Link VCP are the same USART2) and a common ground. Suspect the cable's landing pins first. The `TEMP esc rx:` block in `obc/Src/main.cpp` prints every 2 s until the first status frame arrives; delete it once `ESC: up at N ms, flags=0x0F` appears.

Update this block every working session. Historical detail belongs in `journal.md` - keep this block describing the *current* state, not the path to it.

---

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
| 7 | ADCS: closed-loop attitude control | planned - the actuator hardware is done: closed-loop FOC on a real encoder, reaction-wheel effect demonstrated (2026-07-21). Needs the ESC UART link in firmware, then the control law |
| 8 | Capstone: untethered rig + publish | planned - the satellite-side radio hardware is wired; still needs radio drivers, the ground station box, the PTC fuse, and the battery integration |

**Note on phase ordering:** the hardware got built ahead of the firmware, so Phase 7 and 8 *hardware* is largely complete while their firmware is untouched. The phases still gate on software, not wiring.

---

## Carried work

Items that outlived the phase they were opened in, or that are waiting on a part.

- **PTC resettable fuse (Littelfuse RUEF300 or equivalent) - ordered, not yet arrived.** The main + lead currently runs through a temporary jumper. **This must be fitted before the LiPo is ever connected**; until then the bench PSU's current limit is the only fault protection. The one outstanding hardware item on the satellite.
- **Mechanical work on the gimbal plate:** lazy-susan stiction, flywheel inertia (fill pockets outermost-first, consider a larger radius), and tether routing up the spin axis. See the flywheel notes in `hardware.md`.
- **"Link never acquired" vs "link lost" in telemetry (phase 8, open design question).** `link_lost()` measures against `last_command_ms_ = 0`, so COMMAND_LINK_LOSS (Critical, debounce 1) latches ~5 s after every boot with no ground station and drops the rig to SAFE. That *response* is right - a spacecraft that cannot hear the ground should safe - and it is why bench demos need NOOP keep-alives. What is missing is **observability**: nothing distinguishes "never acquired" from "lost after contact". The status array already separates them (amber vs red, derived from an empty command log), but a heartbeat or log reader cannot. Worth carrying in telemetry once the ground station exists to read it; deliberately not a new fault or mode now, since the derivation already exists and there would be exactly one consumer.
- **Whether `journal.md` and `roadmap.md` stay in the repo when it goes public.** The one question the 2026-07-29 editing pass left open. The argument against was that publishing unbuilt plans is the honest-status trap and that a first-person log contradicts the third-person doc voice - both true, and both fixable by editing rather than deletion. The counter-argument is stronger: the journal's hard-won lessons (I2C bus capacitance versus rise time, WS2812s needing 5 V, the gyro bias measured through its own correction, suspect the newest cable before the silicon, a fault reporter that can hang makes every early failure look identical) are the debugging judgement a reader cannot get from finished code.

---

## Boot / startup hardening

A pass over the reset-to-main path: understand it cold - the boot flow is the bare-metal cousin of a bootloader - and close the gaps the auto-generated CubeIDE startup left. It threads through phases 5-6 plus a future secure-boot item, so it is tracked here rather than buried in one phase.

| # | Item | What it does | Status | Owning req |
| - | ---- | ------------ | ------ | ---------- |
| A | reset-cause report | read RCC->CSR at boot, latch + decode the cause (power-on / pin / brownout / software / watchdog), clear the flags, print it in the boot banner | done, bench-verified | REQ-WDG-002 (first cut) |
| B | fault-exception handler | replace the silent-spin Default_Handler for HardFault / MemManage / BusFault / UsageFault with one that captures the fault context (CFSR + the stacked frame) and does a controlled reset instead of hanging | done, bench-verified | REQ-RT-004 |
| C | clock tree bring-up | HSE -> PLL -> 180 MHz with the matching voltage scaling, flash wait states, and APB prescalers | done, bench-verified | REQ-RT-001 (extends) |
| D | independent watchdog | IWDG started + serviced from a health task; a hang or B's fault path resets the board; demonstrate the bite | done, bench-verified 2026-07-29 | REQ-WDG-001 |
| E | secure boot + VTOR | a bootloader at flash base verifies the app image, relocates SCB->VTOR, and jumps to the app; A/B slots + rollback for safe updates | a design sketch, future | new when built |
| F | FPU / float ABI | confirm the firmware build uses the hardware FPU so the ADCS float math runs in hardware | done - build was already hard-float | - |

Only E remains, and it stays a design sketch. Each landed item went in as its own narrow commit with its requirement and verification.

---

## Design reference (flight software)

The system being built. Forward-relevant for every remaining phase.

### Modes

A small state machine: **BOOT** (power-on + self-checks), **STANDBY** (idle/healthy, awaiting commands), **DETUMBLE** (reduce body rates after deploy), **POINTING** (hold an attitude), **DOWNLINK** (empty the onboard payload buffer over the high-rate link during a contact pass - distinct from the always-on housekeeping beacon), **SAFE** (minimal, conservative, after an unresolved critical fault). Every transition logs timestamp, trigger, from-mode, to-mode, requirement ID, and observed response.

### Faults

Defined once in `common/protocol/state.hpp` (C++) as an index enum. The live set is twelve: COMMAND_LINK_LOSS, ACCEL_GYRO_DROPOUT, MAG_DROPOUT, POWER_DROPOUT, UNDERVOLTAGE, OVERVOLTAGE, OVERCURRENT, TEMP_DROPOUT, UNDERTEMPERATURE, OVERTEMPERATURE, WHEEL_DROPOUT, CAMERA_DROPOUT. fsw holds a fault table (per-fault latch, debounce, severity); the active set ships as a bitmask in `heartbeat_t.faults`. A critical fault that doesn't clear drops to SAFE unless a documented degraded behavior exists. Each fault carries its own policy in that table, so one detect -> debounce -> latch -> respond path serves every fault. Future faults switch on as their inputs land - sensor disagreement and actuator saturation in ADCS, a PAYLOAD/CAMERA dropout with the imaging payload - with no new logic per fault.

**Recovery is ground-commanded (and extensible) - planned, not built.** A latched fault clears only on a CLEAR_FAULT command, never autonomously; latching stops a flapping sensor from toggling the fault. The planned companion is a **RESET_DEVICE** command (arg = device id) so the ground can re-initialize a misbehaving peripheral *before* deciding to clear its fault. It routes through a new `platform::reset_device(id)` PAL **action** - the STM32 backend dispatches to that driver's re-init (the IMU's is essentially `imu_init`, which already does a full DEVICE_RESET + reconfigure; the host backend no-ops). Reset belongs in the PAL because it is something the flight software *does*, like `send_telemetry` - not an input the host would have to fake. Flow: fault latched -> ground RESET_DEVICE -> driver re-inits -> the device's validity returns if it recovers -> ground CLEAR_FAULT -> resume. An autonomous version (a health task that retries a bounded number of times, then escalates to SAFE) can layer on top; ground-commanded is the conservative first cut.

### Key decisions

- **Portable flight logic first**, as host C++, so SIL checks behavior before MCU timing is involved.
- **One MCU as the on-board computer:** the portable C++ cross-compiles onto the STM32 and runs alongside the firmware, reaching hardware only through a **platform-abstraction layer** (simulator on the host, real firmware drivers on the target). Same source for SIL, HIL, and flight - only the backend changes.
- **Nucleo-F446RE** as the OBC / bench node (on hand, onboard ST-Link + USB serial).
- **One control task, everything else feeds it through queues.** The flight software stays single-threaded and identical to what SIL runs, because `Executive::cycle()` being a pure function of its inputs is what REQ-PAL-001/002 and all ten SIL scenarios rest on. Mutexes are for shared *resources* (the I2C bus, SPI3 between the camera and the downlink), never for shared flight state.
- **Comms is two logical links, observed from one side.** A low-rate TT&C link (commands up, telemetry down) and a high-rate payload link (active only in DOWNLINK mode); a UART stands in for the TT&C link today, LoRa + nRF24 later. The satellite only sees its own receive side, so the single onboard link fault is `COMMAND_LINK_LOSS`; downlink-delivery loss is the ground's to detect from sequence gaps. Full picture in `architecture.md`.
- **A mode is a posture, not a parameter (2026-07-29).** Something earns a mode when it changes what the vehicle is *doing*; it does not when it only changes what the vehicle is *deciding with*. SURVEY qualifies - deliberately slewing contradicts POINTING's attitude hold. Vision-based target tracking does not - same control law and actuator, with the error arriving from a different source, so it is a command argument. The cost of getting this wrong is not the extra enum value: it is two modes that behave identically appearing in every fallback list, where narrowing one and forgetting the other goes unnoticed.
- **No live video from the payload, and the reason is the link budget (2026-07-29).** The UART downlink ceiling is ~9 KB/s after framing and housekeeping, so a 7 KB frame caps at ~1.2 fps and the deliberate 4-chunks-per-cycle limit puts it nearer 0.3; the phase 8 LoRa beacon is ~625 B/s, eleven seconds a frame. Real spacecraft capture, store, and downlink during a pass, which is what `payload_data_t` already implements - self-describing chunks, out-of-order safe, resumable. Nor does a CNN fit: the smallest useful TinyML models are ~250 KB of weights with a ~70-100 KB arena at 1-2 s per inference on an M4, against 128 KB of RAM total and a 10 Hz loop.
- **Build the final design, not a worse stopgap.** Prefer the intended approach when it is feasible now over a temporary inferior version that would later be ripped out. The one exception is an optimization gated on infrastructure that does not exist yet. (2026-06-15)

---

## Remaining phases

### Phase 7 - ADCS: closed-loop attitude control

**Goal:** closed-loop single-axis attitude control, shown **both** in NASA 42 **and** on the physical reaction-wheel rig, the two overlaid. The control loop is the highest-priority FreeRTOS task from Phase 6.
**Learn:** rigid-body attitude basics (angular momentum, reaction-wheel torque), a simple PD/PID, 42's flight-software socket, basic sensor fusion.
**Tools:** **42** for the physics; the oscilloscope for the motor-drive PWM and phases; the printed reaction-wheel rig.
**Steps:**
1. **Sim first** - model a single-axis body + reaction wheel + gyro in 42; connect the control loop (running on the host) over 42's socket; watch it detumble and point in the 3D view, and log it.
2. **Then the rig** - the same control law on the STM32 driving the gimbal motor through the B-G431B-ESC1 + flywheel, IMU on the platform, free to rotate on the low-friction mount. Tumble it, the wheel detumbles it and holds a commanded heading.
3. **Overlay** - log both, plot sim against rig for the same controller.
4. **The camera becomes a second rate sensor (decided 2026-07-29).** A small raw frame (~80x60 grayscale or RGB565, a second OV2640 configuration alongside the JPEG one), a horizontal band collapsed to a 1-D intensity profile, cross-correlated against the previous frame: the peak shift is angular displacement in pixels, and over the frame interval that is **deg/s**. Roughly 3200 multiply-accumulates over +/-20 shifts on an 80-wide profile - nothing for a 180 MHz M4F with an FPU, comfortably 10 Hz. Sub-pixel by parabolic interpolation on the correlation peak.
   **This is what finally makes REQ-SNS-003 real.** That requirement says "where redundant sources exist, disagreement beyond a defined threshold shall raise a dedicated disagreement fault", and it has sat planned because the vehicle has no redundant sensor path. Camera-derived rate against gyro-derived rate is one, on the single axis the lazy susan actually has: two independent measurements of the same physical quantity, a threshold, and a fault.
   **Not a mode - a sensor.** It feeds `Inputs` alongside the IMU with its own validity flag.
   Two prerequisites and one bench constraint: the **payload task + SPI3 mutex** must land first (reading 80x60x2 = 9600 bytes at the measured ~500 KB/s costs ~20 ms, a fifth of the control cycle, and it must not spend that inside the control task); the raw register tables need vendoring the way the JPEG ones were; and **cross-correlation needs texture** - a blank white wall has no features and returns no peak, so the bench needs a poster or a cluttered background before anyone debugs "why is the rate always zero".
**Done when:** a clip and plots show the same controller detumbling and pointing in sim and on the rig, and the two rate sources tracking each other.

### Phase 8 - Capstone: untethered rig + demo package & publish

**Goal:** the rig runs free - battery-powered, no USB tether - holding the detumble/point demo standalone while a **dual-link** comms stack runs: an always-on LoRa beacon (mode/faults/telemetry) plus a high-rate nRF24 payload downlink that empties the imaging buffer to the ground station during a contact pass. Then the polished package and a public repo.

**Steps:**
1. **The remaining OBC tasks.** Uplink and telemetry tasks - the uplink blocks on UART RX instead of polling every cycle, and one task owns all console output (`configUSE_NEWLIB_REENTRANT` is 0, so `vsnprintf` from two tasks is not safe). This is where the first `FromISR` call appears, and why the UART interrupt priorities went in first. Then the **payload downlink task**, and with it the **SPI3 mutex** - the camera is still read from the control task precisely because the downlink shares that bus, so the mutex and the task land together. `scan_jpeg_length()` in `ov2640_poll()` scans the whole ~8-10 KB FIFO byte-by-byte over polled SPI from inside the control task, costing that one cycle ~16 ms of its 100 ms budget (inferred from a 16 ms timestamp shift, not yet timed directly - do that before quoting the number anywhere public). No deadline is missed, so this is a margin argument rather than a violation, but it is margin the phase 7 control law will want.
2. **Radio drivers** - the largest chunk. Bring up the two transports behind the existing framed/CRC protocol (swap transport, frames unchanged - REQ-TLM-004): the RFM95/LoRa beacon first, then the nRF24L01+PA+LNA high-rate link.
3. **The ground station box.** The owned Teensy host, both receivers, and the SSD1306 OLED moved from the satellite, bridged to the laptop over USB. Its firmware lands as a new top-level `gsw/` in C++ (the sibling of `fsw/`) and shares the wire contract already at `common/protocol/`; `tools/` Python stays the ground brain (decode, logs, reports) and the Teensy is the bridge and console. **No command buttons and no battery (decided 2026-07-29):** the laptop console the box is already tethered to is a strictly better command interface, and the cable that carries those commands carries the power. The satellite is the thing that goes untethered; the ground station staying wired is what makes that demonstration mean something.
4. **Battery integration.** Measure the assembled current draw (motor peaks + logic + both radios transmitting) and size the pack; fit the PTC fuse; run on battery, perturb the platform, watch it recover and downlink an image over the wireless link.
5. **The package.** Collect the verification matrix, SIL/HIL reports, plots, the sim-versus-rig overlay, and a demo clip; polish the README; make public.

**The capstone imaging demo is a slew-and-image panorama, in a new SURVEY mode (decided 2026-07-29).** Command a controlled slew, capture at intervals through the rotation, downlink the set, and stitch it on the ground into one wide image. Slew-and-image is how a lot of smallsat imaging actually works, and it *uses* the attitude control rather than duplicating it, so almost all the new code is a ground-side stitcher. The result is one image that took attitude control, payload timing, and a multi-image downlink to produce.

**Why SURVEY is a mode when target-tracking is not.** The test: does it change what the vehicle is *doing*, or only what it is *deciding with*? A survey means deliberately slewing at a commanded rate, which directly contradicts POINTING's job of holding still - different control law, different risk, and a POWER_DROPOUT mid-survey should abort the slew rather than do what it does in POINTING. Target tracking is the same control law and the same actuator with the attitude error arriving from vision instead of an inertial setpoint, so it belongs as a **source selected by a command argument**, not a seventh mode. Two modes differing only in an input is exactly the duplication that lets someone narrow one fallback list and forget the other - the failure SIL-010 was written to catch. Adding SURVEY costs one `FSW_MODE_LIST` line (everything derives from the x-macro, and the drift tests catch the Python mirror), a row and column in `kAutoAllowed`, an LED bead case, the console ladder, a review of the three degraded-fallback lists, and its own scenarios.

**Autonomous capture triggering, if it is cheap by then.** Downsample, sum absolute differences against the last frame, and capture full-resolution only when the scene changed. Not a mode - a policy inside the payload path. Worth it for the line it earns: the spacecraft deciding which frame is worth the downlink budget, which is the honest answer to why this vehicle does not stream video.

**Done when:** the untethered demo is captured (recovery plus an image downlinked over the high-rate link), the verification matrix is complete, and the repo stands on its own.

---

## Simulation - NASA 42 (Phase 7)

The simulator is **NASA's 42** (open source, GSFC, by Eric Stoneking; `ericstoneking/42`). One tool, committed to.

**Why:** it models exactly what's needed - rigid-body attitude dynamics, reaction wheels, gyros, magnetometers, and the space environment - and has a **socket interface** so external flight software reads sensor data and sends actuator commands each timestep. Lightweight C, runs on Linux/WSL, with a 3D OpenGL view.

**How (Phase 7):** model a single-axis rigid body + one reaction wheel + a gyro in 42 (same setup as the bench); 42 runs the physics and outputs simulated gyro/attitude each step; the flight software's controller - the same control law that runs on the STM32 - connects over the socket, reads the gyro, computes a wheel-torque command, and sends it back. Watch it detumble and point in the 3D view, log the response, then run the same controller on the physical bench and overlay the two. "Same code, simulated satellite and real hardware" is the demo.

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
| 7 | attitude control / reaction wheel; sensor fusion; NASA 42 simulation; Fusion 360 |
| 8 | LoRa wireless link; untethered demo; full V&V package |
