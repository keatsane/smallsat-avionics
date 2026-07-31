# Journal

Two things worth keeping: what was learned the hard way, and a dated record of what was proven
when. Reasoning for the current design is in `architecture.md`; what is left to build is in
`roadmap.md`.

## Lessons

Each of these cost real time and would change what someone does next.

**Suspect the newest cable before the silicon.** Three multi-hour debugs on this build were
marginal cables, and the electrical tests on the components came back clean every time, because
the components were fine.

**A powered device with no ground reference takes down its neighbours.** The camera's ground
arrived only on its SPI3 connector; unplugging that left it floating at ~500 mV and hunting for a
return through its SCCB pins, which pushed the INA228 off the shared I2C bus. Cost a day in July
and was only explained a week later. Every device gets power and ground on the same connector.

**A green build says less than it looks.** `file(GLOB)` is evaluated at configure time, so a new
source silently linked against the old file list - three sessions lost before `CONFIGURE_DEPENDS`.
And `--gc-sections` drops code nothing calls: the whole LoRa driver compiled and was absent from
the image, with `nm | grep` the only symptom.

**An open-loop test checks what a controller says; only a closed loop checks what it does.** The
detumble law had its sign backwards - commanding a wheel torque whose reaction accelerated the
platform - and every unit test passed, because a formula test cannot tell which way a reaction
pushes.

**`||` short-circuits, so a state-machine step on its right only runs sometimes.** `rfm95_tx_done`
is what returns the radio to receive; behind `!pending ||` it ran once a second and the vehicle
was deaf almost continuously. Side effects do not belong in conditions.

**Two correct instructions can compose into a trap.** The bias needed two still seconds at boot;
the compass needed a calibration turn right after power-up. An operator doing both in the told
order poisoned the bias with the turn, and the vehicle wedged itself detumbling a spin that
existed only in the arithmetic. Calibration steps that share a clock must be checked against each
other, not just each against the hardware.

**Never pace a half-duplex transmission off the other end's frames.** A request triggered by the
vehicle's own status frame launches phase-locked to the vehicle's transmit schedule, and if the
phase is wrong it is wrong every time - eight consecutive selective-repeat requests landed in the
vehicle's deaf window. The command retries already knew this and used 0.7 s against a 1 s beacon;
the requests were written a week later and stepped straight into it.

**A signal that is merely plausible is not a signal that is right.** The control path read gyro z
as yaw for five weeks, and z read plausibly - noise at rest, small wiggles when touched - because
cross-coupling gives every axis a shadow of the real motion. Only spinning the platform and
comparing all three channels showed the spin lived on x. Check which channel actually carries the
physics before trusting the one the datasheet suggests.

**A one-way link needs repetition, not a better error rate.** 88% packet delivery sounds like a
working link and delivered zero images: a frame spans three packets, an image needs every frame,
and there is no ack to retransmit against. Losses fall in different places each pass, so sending
the same frame three times is worth far more than the arithmetic suggests.

**Two PA+LNA radios a foot apart is a receiver problem, not a range problem.** At full transmit
power delivery was 39%; at minimum it was 88%. An overloaded low-noise amplifier does not degrade
gracefully, it stops decoding, so the symptom looks like a weak link and the fix is less power.

**A receiver's fifo depth is a rate limit on the transmitter.** The nRF24 holds three packets and
a frame spans three, so anything the ground could not collect in time took a whole frame with it -
588 packets sent, 379 received, one frame decoded. Independent packet loss would have left a
quarter of the frames intact; losses in runs left none. Pace the sender to what the receiver can
take.

**A health message sent once, at boot, is not observability.** The ground station said whether
its radios came up exactly once, before anyone had opened a terminal - so a dead payload receiver
and a working one hearing nothing produced identical evidence for three sessions, while the
vehicle transmitted whole images into it. Anything that can be false later has to be said again.

**A shadow copy of a byte stream drifts from the parser reading it.** The ground station buffered
received bytes to forward "the frame the parser just finished", but nRF24 packets are padded to 32
and a frame is 70, so the padding accumulated in the shadow buffer and every forwarded frame was
padding plus a truncated frame. No image ever reassembled. The parser already knows the frame -
re-encode from it and there is nothing to drift.

**Half duplex means a scheduling constant decides how deaf the vehicle is.** The radio cannot hear
while transmitting, and leaving the return-to-receive to whoever next polled put the telemetry
task's 50 ms idle wait on top of 57 ms of air time. One uplinked command in six was lost. The
transmit now waits for its own completion, and the ground retransmits until acked.

**A completion flag reads zero before the first operation.** Gating the first transmit on "has the
last transmit finished" deadlocks forever. The beacon never sent a single packet.

**A task that gains work needs re-measuring, and nothing prompts you.** The health task went under
its stack floor twice in two days, the second time immediately after writing that lesson down. The
number sits in every TASKS line until somebody reads it.

**A checker only run against a passing repo has not been shown to catch anything.** The PAL
boundary check's first regex could not match `NVIC_EnableIRQ`, because `_` is a word character - so
every underscored form, which is how these names are actually written, passed silently.

**Read the vendor's source rather than reasoning from the register map.** Three separate camera
bugs were found by diffing against ArduCAM's own sequence after reasoning had failed.

**Estimates can be wrong in the direction that matters.** Guessed inertias put the wheel's
authority at 3.6 rad/s of platform rate; measured, it is 0.87. Two errors compounding, and every
conclusion drawn before the measurement was out by 4x.

**A fault with no responder is a catalog entry.** Observability first, response when there is
something to respond with.

**Dates come from `git log`, not memory.** Entries here drifted into the future more than once.

## Log

Dated evidence, newest first.

### Phase 8 - wireless link and ground station

- **2026-07-31** - First battery boot: 16.75 V on the main bus through the new PTC fuse, radio
  telemetry with no bench supply. The same session found the bias trap it sprang: the gyro bias
  was learned from the first two seconds after power-on, and a boot that happened while the
  platform was being handled read a phantom 144 deg/s at rest forever - autonomous detumble
  latched on a spin that did not exist. Bias now locks only from a window that stayed quiet end
  to end. And the ground can now `poll` one frame of any telemetry kind over the beacon
  (REQ-TLM-006), since the full sensor stream can never ride the low-rate link.
- **2026-07-30** - The console grew a mission: `shoot [size] [bearing]` walks point, aim,
  capture, downlink, park as one command, advancing each step on telemetry evidence rather than
  on having sent something. The compass gained a spike gate and smoothing after bench headings
  repeated only to tens of degrees - min/max calibration never forgets, so one glitched sample
  had been corrupting the spans for the rest of the boot. README and roadmap caught up with the
  radio arc they predated.
- **2026-07-30** - **The untethered pass - phase 8's deliverable.** No data cable on the vehicle:
  commanded into POINTING over LoRa, captured, and downlinked over nRF24 to
  `captures/image_0017.jpg`, markers intact. First pass delivered 72%; two selective-repeat
  rounds (10 missing, then 1) finished it, which is the protocol doing exactly its job. The same
  log shows auto-detumble catching two hand spins and handing the vehicle back, and pointing
  holding its captured heading to a fraction of a degree at rest. Power was the bench supply -
  the battery waits on its fuse.
- **2026-07-30** - Selective repeat closed the loop on the bench: two 800x600 images arrived
  complete over the air, gaps named and refilled. The gyro moved to +-2000 dps after fast spins
  clipped at 1000 and the heading "lost track"; a compass disagreement past ~57 degrees now snaps
  the estimate instead of dragging it back. The display convention (clockwise for an observer
  looking down) enters at exactly one constant, and a commanded SET_HEADING now outlives mode
  changes rather than being forgotten on re-entering POINTING.
- **2026-07-30** - The bench falsified two of the day's guesses. The compass sign, hand-picked
  twice and wrong twice, is now measured: the firmware correlates the mag angle against the gyro
  during the calibration turn and locks whichever sign agrees. And selective repeat's requests
  never arrived because they were triggered by the vehicle's own 1 Hz status frame - phase-locked
  into its transmit window, eight for eight lost. Requests now run on the ground's clock at 1.3 s,
  so no alignment survives a round.
- **2026-07-30** - DETUMBLE became autonomous protection: any operating mode that starts spinning
  pulls itself into the recovery and resumes where it was (REQ-MODE-012, SIL-014). The operating
  modes became a clique - the sequencing ladder encoded a deployment story the rig does not have.
  The compass gained live hard-iron calibration after the first bench run showed its field circle
  centred at +300 counts; it needs one full turn of the platform after power-up before it speaks.
- **2026-07-30** - The design audit's structural items landed. Heading became absolute: a
  complementary filter blends the gyro integral with a magnetometer compass heading, running in
  every mode, so the dial tracks the platform even in STANDBY and SET_HEADING names a repeatable
  bearing. DETUMBLE exits itself to STANDBY after a second inside the deadband (REQ-MODE-011,
  SIL-013). The executive's three retreat clauses collapsed into a fallback table.
- **2026-07-30** - The yaw axis is the IMU's X, not its Z: a bench spin swung gyro x by +/-10000
  counts while z saw tens of cross-coupling - the board is mounted x-vertical, and the control
  path had been watching the wrong channel since the plate was built. Bias is now learned from
  the first two seconds at rest, which was worth a quarter degree of heading every ten seconds.
- **2026-07-30** - The downlink became a protocol. Blind three-pass repetition replaced with
  selective repeat: one pass, then the ground names its missing chunks over the LoRa uplink and
  the vehicle resends exactly those out of the fifo it never consumed. Same audit fixed the
  pointing error not wrapping (a 216-degree command drove the long way round), made DETUMBLE
  commandable from both active modes, and stopped the console mangling what it echoes.
- **2026-07-30** - **First image down over the air**: 800x600 commanded over LoRa, 398 chunks over
  the nRF24, 22240 bytes reassembled with intact markers at 96% packet delivery. Three passes over
  the same frame is what closed it.
- **2026-07-30** - Payload link fixed and images reaching the ground station. Minimum transmit
  power took delivery from 39% to 88%: two PA+LNA modules a foot apart were overloading the
  receiver's front end, not struggling to reach it. 88% still delivers no image, because a frame
  spans three packets and an image needs every frame - so a downlink now makes three passes over
  the same frame, rewinding the ArduChip's read pointer rather than recapturing.
- **2026-07-30** - `CAPTURE_IMAGE` takes a size (320x240, 800x600, 1600x1200), `SET_HEADING` aims
  POINTING at a bearing, and `attitude_status_t` feeds a dial on the second panel. The payload
  radio dropped to minimum transmit power: two PA+LNA modules a foot apart delivered 224 of 570
  packets at full output, which is an overloaded receiver rather than a weak link.
- **2026-07-30** - Payload link proven, and the first real numbers off it. Strapping the second
  OLED to 0x3D exposed that `Adafruit_SSD1306::begin` reports a panel on an address with nothing
  on it, so the bus is probed directly now. With the link up, an image downlink put 588 packets on
  the air and 379 arrived - and only one frame decoded, because a frame spans three packets and
  the losses come in runs. The transmitter is paced from here.
- **2026-07-30** - The vehicle was transmitting the whole time. A downlink progress frame on the
  LoRa beacon reported 186/186 chunks and 549 nRF24 packets while the ground received nothing, so
  the failure is the ground station's receiver and never was the satellite. The ground station now
  emits its own health once a second rather than once at boot, which is what hid this: a receiver
  that failed to initialise printed one line before anybody had opened a terminal, and afterwards
  looked exactly like a working one hearing nothing. Beacon queue went to three deep in the same
  change - a heartbeat landing on a pending command ack was evicting it.
- **2026-07-30** - Documentation cut roughly in half and two link bugs fixed. The ground station
  was forwarding padding plus truncated frames on the payload link, which is why no image ever
  arrived; it now re-encodes each decoded frame. `rfm95_send` waits for its own transmit to finish
  instead of leaving the radio in standby until something polled, and the ground console
  retransmits an unacknowledged command, which the flight software makes safe by answering a
  repeated sequence number with the previous verdict rather than acting twice (REQ-CMD-003).
- **2026-07-30** - Link runs both ways. Beacon and command acks on LoRa, payload chunks on nRF24,
  commands up from the ground console into the same uplink task that decodes them from a uart. The
  ground station is a Feather M0 RFM95 compiling `common/protocol/` rather than reimplementing it,
  with an SSD1306 showing mode, faults and link with no PC attached. **Owed: the untethered run.**
- **2026-07-30** - Legacy sweep. Radio health telemetry filled the `LoraStatus`/`Nrf24Status` ids
  reserved since June; SPI arbitration moved to a per-peripheral lock; CubeIDE project files and
  the ESC probe env removed; the PAL boundary became an automated check (REQ-PAL-001).
- **2026-07-30** - Requirement backlog cleared: REQ-WDG-002 and REQ-TLM-005 bench-verified,
  REQ-SNS-003 deferred as conditional on redundancy this build does not have.
- **2026-07-30** - Camera dropout test (REQ-PAY-002), which produced the ground-reference finding
  above. Fault bead confirmed orange with a Degraded and a Warning fault latched together.

### Phase 7 - ADCS

- **2026-07-30** - Plant model, closed SIL loop, detumble and pointing laws. SIL-011 nulls a
  2 rad/s spin inside the deadband; SIL-012 shows a sustained disturbance saturating the wheel.
  Inertias measured from Fusion (j_wheel 1.09e-4, j_platform 4.53e-3 kg m^2); friction fitted to a
  hand-timed coast-down, ~6.28 rad/s to rest in ~2 s. **REQ-ADCS-002 deliberately not claimed:**
  at 10 Hz with rate-only sensing a 0.35 rad/s shove moved the platform 0.117 rad while the gyro
  read zero at every sample. A sensing limit, not a control limit.
- **2026-07-30** - NASA 42 evaluated and set aside; reasoning in `roadmap.md` under Simulation.

### Phase 6 - real-time task model

- **2026-07-30** - Telemetry and payload downlink became their own tasks, and
  `kPayloadChunksPerCycle` was deleted as a link fact that had leaked across the PAL. A 6117-byte
  frame went from ~3 s to ~1 s. The same run exposed a latent watchdog reset: the downlink task
  reported liveness once per image rather than once per chunk, which on a full-resolution frame
  would have reset the board mid-contact.
- **2026-07-30** - Uplink moved off the control task; the project's first `FromISR` call. Fault
  inhibits reached the wire, and an undervoltage sweep closed REQ-FAULT-002, REQ-FAULT-001,
  REQ-MODE-006 and REQ-HMI-001 in one run - latched at 13.6 V, SAFE within one heartbeat, no
  autonomous climb out, recovery a deliberate two-step by ground command.
- **2026-07-29** - Phase 6 closed. HIL-003 passed 7/7: watchdog fed in all 60 reports, worst stack
  margin 104 words against a 64 floor, 59 heartbeat periods at a 1.000 s mean. The bite was
  demonstrated separately (`reset=iwdg-watchdog`). Stacks sized against measured peaks.

### Phase 5 - sensors and payload

- **2026-07-29** - The whole payload arc on real silicon: commanded capture in POINTING, then 121
  chunks in DOWNLINK reassembled to a 6759-byte jpeg with markers intact at both ends.
- **2026-07-28** - Every mode commanded on hardware. BOOT was a dead end - nothing ever requested
  BOOT -> STANDBY - which the mode ladder test found (REQ-MODE-010).
- **2026-07-27** - Camera produced a real 7688-byte jpeg. The ArduChip's length register is **not**
  the image size: two captures of different scenes both reported 7688 while the jpeg inside
  differed by ~100 bytes, so the driver scans for FF D9 rather than trusting it.
- **2026-07-26** - Satellite hardware build complete, all three plates. WS2812s need 5 V; a 3.3 V
  supply is below their minimum and they simply do not light.
- **2026-07-21** - Gimbal plate complete: closed-loop FOC on a verified AS5600 encoder, and the
  reaction-wheel effect demonstrated at up to ~344 RPM.

### Phases 0-4 - foundation

- **2026-06-11** - Phase 4 complete. Both HIL scenarios passed on the live board, with the flight
  software running unmodified on the STM32 behind the PAL.
- **2026-06-10** - Phase 3 complete: the scenario runner. Phase 2's executive and comms before it.
- **2026-06-09** - The wire contract settled at `common/protocol/` in C++, so the ground firmware
  could be a genuine second consumer rather than a copy.
- **2026-06-07** - Dual-link comms decided: an always-on LoRa TT&C beacon plus an nRF24 payload
  downlink active only in DOWNLINK. That split is what makes DOWNLINK a real mode.
- **2026-06-06** - Requirements-first pass; hardware standard finalised and ordered.
