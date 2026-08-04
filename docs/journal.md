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

**A control gain belongs to the plant, not to the mode using it.** POINTING's inner loop is
DETUMBLE's law with a moving setpoint, and it was given a gain twelve times the one the rig had
already accepted, because the slew needed more torque to break the bearing loose. The platform
does not care which mode is asking - it oscillated at 215 deg/s. The two gains are one gain and
are now pinned together in the header with the reason.

**Friction is not answered by feedback.** Below the bearing's breakaway the platform does not move
at all, and the gain large enough to clear it is the gain that oscillates - the two windows do not
overlap, so no amount of tuning finds a value in between. What works is a constant: a fixed
feedforward in the direction of the error, applied only while the platform is stopped, which
supplies authority without adding any loop gain. That took POINTING from thrashing to converging
in one change, after two sessions of tuning the wrong knob.

**A measurement taken through a slower stream than the event is not a measurement.** The
breakaway sweep asked the ground to compare the heading before and after each pulse. The pulse
lasts half a second, the platform breaks loose and friction stops it inside a second, and the
radio delivers one attitude frame per second - so a 13 deg/s jolt was recorded as "no movement"
while the heading crept visibly across the whole sweep. The vehicle watches its gyro ten times
faster and simply knows; it reports the peak now instead of the ground guessing from samples.

**The bench rig is part of the plant, and its cables are a spring.** A breakaway sweep looked like
friction until the platform kept returning to where it started after every pulse - dissipation
does not do that, elasticity does. The USB cable feeding the board was a torsion spring across the
rotating joint. Any attitude measurement taken with something plugged into the vehicle is
measuring the tether too, which is the reason the rig is built to run on battery.

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

### Phase 8 - the capstone

- **2026-08-04** - **The survey flew; the panorama did not.** One typed word - `survey 4 60` - and
  the vehicle pointed itself, captured, downlinked 400-odd chunks with selective repeat, parked in
  STANDBY to unwind its wheel, slewed, and did it again. Three frames landed, unattended, on
  battery, over the radio, with nothing plugged into the vehicle. The sequencing works.
  The picture does not. The frames came from roughly 0, 9 and 28 degrees against a commanded 0,
  20 and 40, because a slew that size runs the wheel out of momentum before it arrives - so they
  overlap almost completely and the stitched strip is three near-identical photographs rather than
  a swept scene. It was tempting to keep it anyway; a file called `panorama.jpg` that is not one
  is exactly the kind of claim this repo is built to not make, so the output and its frames were
  removed and REQ-PAY-006 says "in progress". The macro and the stitcher stay - what they need is
  bearing separation, and that is an actuator problem, not a sequencing one.
- **2026-08-04** - Three ground-side bugs stood between the survey and that result, and all three
  were the same shape: **believing something held when it did not.**
  The aim step trusted an in-band flag that was still describing the *previous* bearing, so the
  first attempts shot twice in the same place; the fix is a handshake, waiting for the vehicle to
  echo the target it is holding. The keepalive treated selective-repeat chunk requests as "the
  link is busy", but those never reach the command handler, so the ground chattered for nine
  seconds while the vehicle heard silence and safed at exactly five. And momentum dumping was
  written against the ESC's velocity, which reads +/-3 rad/s at a standstill - so a vehicle idling
  in STANDBY spun its wheel *up* on noise, and every leg started from an arbitrary wheel state.
  The last one is the sharpest: the magnetometer gate had learned that exact lesson an hour
  earlier and switched to reading the wheel's angle. Knowing a signal is untrustworthy does not
  help if the next thing you write reaches for it anyway.

### Phase 7 - closing the loop on the rig

- **2026-08-04** - **The wheel can be emptied now.** REQ-ADCS-003 had a clamp and no answer: the
  rig had shown a saturated wheel holding full commanded torque for fifteen seconds with the rate
  pinned at zero, and nothing in telemetry said why. `WHEEL_SATURATED` latches at 90% of the
  wheel's measured top speed, retreats the steering modes to STANDBY, and STANDBY unwinds the
  wheel against the platform's own bearing - any torque under breakaway spins the wheel down
  without moving the body, so the stiction that makes pointing hard is what makes the wheel
  rechargeable. The retreat goes to STANDBY *because* that is where the cure lives.
  Closing it turned up a gap in the harness worth its own note: the plant has modelled the wheel's
  speed since it was written and never reported it to the flight software, so every scenario ran
  with the actuator's own state invisible. An actuator a simulation steers but does not report is
  one whose failure modes cannot be graded - SIL-012 went from 7 checks to 9 the moment the shim
  passed it through.
- **2026-08-04** - **The controller earns its keep: 2.5x friction, measured.** The same ~90 deg/s
  spin was run in SAFE, where no torque is commanded and autonomous entry is off, and in STANDBY,
  where it pulls the vehicle into DETUMBLE. Friction alone: 161 deg/s^2. With the wheel:
  412 deg/s^2. That closes the gap REQ-ADCS-001 had been carrying - a platform that stops itself
  makes "it stopped" worthless as evidence, and the only way through it is a controlled
  comparison. None of it was measurable at the heartbeat's 1 Hz; the entire decay was one sample
  until the attitude telemetry moved to the control rate.
- **2026-08-04** - **The plant model's friction was right all along, and it took two bad
  "corrections" and an overlay to find that out.** The morning's breakaway sweep was read as
  bearing stiction and coulomb friction was pulled from 13 mN m down to 4; then a mis-read of the
  A/B decay moved it to 8. Both were wrong. The first ignored that the wheel does not rotate below
  ~5 mN m commanded, so the sweep had found the motor's cogging as much as the bearing's stiction.
  The second averaged a stretch of the decay where the platform was still being pushed by hand,
  giving 120 deg/s^2 where the arrest alone is 161. Re-fitting on the corrected number lands at
  12.3 mN m - within a few percent of the original value, from a single measurement and a prior.
  What caught it: `tools/overlay.py` ran the model and the rig through the same coast and they
  disagreed by 1.5x, which is not something a model fitted to that very run can do. **A plant
  model checked only against itself will absorb a bad measurement without complaining.** The
  overlay is now a scenario of its own (SIL-015) so the comparison runs with the suite.
- **2026-08-04** - **Breakaway torque measured: 5 mN m.** The vehicle pulsed its own wheel at
  rising torque over the radio with nothing plugged into it, and reported the peak body rate each
  pulse produced: 4 mN m moved it 0.1 deg/s, 5 mN m turned it at 7.5. The plant model had guessed
  16.3 - a factor of three - because a coast-down only ever sees sliding friction and the sticking
  case was 1.25x that, a ratio sitting on top of a fit. Re-fitting with both measurements
  separated the terms properly for the first time: the coast-down pins total drag at speed, the
  sweep pins the sticking case, and what is left has to be viscous.
  **SIL-011 promptly failed**, which is the harness doing its job. It had been nulling a 2 rad/s
  spin, and with three times less free drag the wheel ran 36.02 of 36 - it had been passing on
  borrowed friction. At 1.6 rad/s it peaks at 27.8, so the honest statement of this vehicle's
  authority moved from "a 2 rad/s upset" to "about 1.6 rad/s, 90 deg/s". A unit test that assumed
  10 mN m was below breakaway failed in the same pass, for the same reason.
- **2026-08-04** - Getting that measurement took two tries, and both failures were instructive.
  The first tool drove the ESC over USB - but the USB cable *is* the tether, and a cable across
  the rotating joint is a torsion spring, so it measured the cable. The second put the sweep on
  the radio but had the ground compare heading before and after each pulse: the platform's answer
  is a transient that starts and ends inside a second, and the radio carries one attitude frame a
  second, so a real 13 deg/s jolt was recorded as "no movement". The fix was to stop inferring -
  the vehicle now reports the peak rate it saw since its last pulse, watched at the control rate.
  Measure where the data is.
- **2026-08-04** - **POINTING works on the rig.** Commanded to a bearing 28 degrees away, on
  battery with nothing plugged in and everything over the radio, the platform converged
  monotonically - error -27.9, -21.8, -16.3, -12.5, -5.3, -2.0, -0.7 - and held at 0.7 degrees
  against a 2.9 degree band, with no overshoot. It gets there by inching: the feedforward breaks
  the bearing loose, the platform moves, the push drops out, friction stops it, repeat. That is
  the honest gait for a wheel this size on a bearing this stiff.
- **2026-08-04** - Getting there took two wrong turns worth keeping. The pointing law was a PD on
  angle whose gain saturated the wheel at 7 degrees of error, so it was a relay rather than a
  controller and the rig answered with 130 deg/s slams and 19 degrees of overshoot; it became a
  cascade, an outer loop asking for a bounded slew rate and an inner loop that is detumble's law
  with a setpoint. Then that inner loop was given a gain twelve times detumble's measured one, to
  give the slew enough torque to break stiction, and it oscillated at 215 deg/s - the same mistake
  as the morning's, larger. The answer was not a gain at all but a constant feedforward.
- **2026-08-04** - POINTING and DETUMBLE were fighting each other. A slew is body rate on purpose,
  and it passed the autonomous-detumble threshold long before reaching the target, so the vehicle
  cancelled its own manoeuvre, resumed, and started again - forty seconds of that without
  arriving. REQ-MODE-012 now excludes POINTING: detumble answers rate nobody asked for.
- **2026-08-04** - The magnetometer is held off while the flywheel turns. Its rotor magnets sweep
  the field past the sensor at eleven times shaft rate, far above the 10 Hz sample, and the
  heading walked ~5 deg/s with the gyro reporting the platform was still. The first version of the
  gate asked the ESC's velocity, which reads +/-3 rad/s at a standstill, so it never opened at
  all; it asks the wheel *angle* now, which sat on the same count indefinitely. Attitude telemetry
  went to every control cycle in the same pass - a 10 Hz loop cannot be judged from 1 Hz samples,
  and a whole detumble was falling between two lines.
- **2026-08-04** - The last bench inhibit is gone. With the ground station holding the command
  link, COMMAND_LINK_LOSS came off the list and the vehicle now runs its real fault responses -
  which promptly safed it five seconds into the first quiet run, correctly. The console holds the
  link itself now, on 1.3 s of quiet rather than a 2 s metronome: 2 s is a harmonic of the 1 s
  beacon, so every keepalive landed in the same deaf window and none of them arrived. That trap
  was already written down two lessons above, and it still got walked into.
- **2026-08-03** - **The wheel is back.** The replacement B-G431B-ESC1 flashed, aligned FOC, and
  reported all four health flags; 2 V on the q axis spun it to 14.7 rad/s. The first upload failed
  with `open failed` because `platformio.ini` still pinned the dead board's ST-Link serial - the
  pin exists so an upload cannot land on the Nucleo, and a board swap is exactly when it bites.
  Wired to the OBC on USART1 crossed, `ESC: up at 5334 ms, flags=0x0F`; the 8 s acquire window
  written for the ESC's cold FOC alignment covered it, so no dropout latched. `WHEEL_DROPOUT` came
  off the bench-inhibit list and the temporary rx diagnosis block came out with it, leaving
  COMMAND_LINK_LOSS as the only inhibit. The rig is hardware-complete for the first time.
- **2026-08-03** - The wheel's sign was measured rather than assumed: positive q-volts turns the
  platform counterclockwise seen from above, which against the compass convention (clockwise
  positive) makes `tau = +k_rate * omega` damping, as written. The header comment had claimed
  `tau = -k_rate * omega` - the opposite of the code, in the one place a sign error already cost a
  session - and was corrected. The platform swinging ~15-20 deg and then stopping while the
  flywheel kept spinning is the momentum-exchange signature: reaction torque exists only while the
  wheel's speed is changing, and the platform's own stiction absorbed the rest.
- **2026-08-02** - The ground station got its CAD: `ground_plate`, `ground_cover`, and a
  `ground_assembly` tying them to the protoboard and the vendor OLED model. `stack_assembly`
  became `satellite_assembly`, since there are two assemblies on the desk now. Fusion stamps a
  fresh timestamp into every export, so a byte compare calls all 46 parts changed every time;
  `tools/cad_sync.py` compares only the geometry, and this drop touched exactly five files.
- **2026-07-31** - The one-word mission ran on battery: `shoot` walked point, capture, downlink,
  and park by itself, 451 chunks at 95% first-pass delivery, two request rounds, and
  `image_0018.jpg` on disk in twelve seconds. The first over-the-air `poll POWER` closed
  REQ-TLM-006, and switching the vehicle off put LOST and the age of the silence on the ground
  station's screen instead of a confident ghost.
- **2026-07-31** - The heartbeat carries the bus voltage now: on battery it is a vital sign, and
  the ground station's status panel draws it as a battery gauge mapped to the working range
  (13.6 V "land now" to 16.8 V full). The same panel pass gave both screens fixed regions, and
  the big word finally tells the truth about a dead link - LOST plus the age of the last
  heartbeat, instead of confidently showing the last mode of a switched-off vehicle.
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
