# Hardware & mechanical build

The physical "satellite" - a representative (not flight) CubeSat-style structure that holds the avionics, run on the bench. This file covers the physical objects and the mechanical constraints on them: what a part demands of its neighbours, where everything goes on the stack, and what gets printed. The parts themselves are listed in [bom.md](bom.md); the buses each one sits on, the stack architecture, and the reasoning behind the choices are in [architecture.md](architecture.md); what wire lands where is in [wiring.md](wiring.md).

---

## Part constraints

Standing facts about the parts themselves, as opposed to things that went wrong once - those are in journal.md, dated and with the symptom that found them.

- The **nRF24L01+PA+LNA** needs a 10 uF cap (10-47 uF) across VCC/GND at the module. The PA pulls ~115 mA in bursts and browns out without it. Power it from 3.3 V, not 5 V - its logic inputs are 5 V tolerant, VCC is not.
- **No radio transmits without an antenna attached.** TX into an open ANT can damage the power amp.
- The **WS2812 beads are 3.5-5.3 V parts**, so they run off +5V (CN7-18) with 3.3 V data straight in. A 3.3 V supply is under minimum and they never light, however clean the data.
- **The WS2812 pigtail silkscreen is a trap:** "DO" reads as "D0" and "DI" as "D1" at that size, which inverts the meaning. Establish data direction by continuity from the LED chip's own pins to the pigtail wires, not from the printing. Getting it backwards is harmless - nothing lights, nothing is damaged - and the fix is to feed the other end of the chain.
- The **AS5600 needs a diametrically magnetized magnet** centred on the rotor - an axial disc will not work - and its DIR pin tied to GND at the board. Floating DIR gives undefined rotation direction.
- **USART2 (PA2/PA3) is the ST-Link VCP** and is isolated from the header pins by default (UM1724 solder bridges). Anything that needs probing on a header goes on USART6.
- The **INA228 reads ~18% higher current than the bench PSU's display** (200 mA against 170 mA; power tracks it). Voltage agrees to 0.4% and the INA's own V x I = P is self-consistent, so the discrepancy is the current channel alone - most likely the PSU's ammeter, since 170 mA is 3.4% of its 5 A full scale while the INA228 is a 20-bit part good to ~0.5%. Not worth fixing: thresholds are sized from observed readings, so a systematic offset cancels. Arbitrate with a handheld meter in series before trusting `SHUNT_CAL` for energy accounting.

## Tools

Owned bench kit: Siglent SDS804X HD scope, USB logic analyzer, multimeter, adjustable bench PSU, JST-SH crimp kit. What each is for, when it shows up, and what to practice on it:

### Bench instruments

- **Oscilloscope - Siglent SDS804X HD.** 4-channel, 12-bit. The main analog-signal and timing tool. *First use (Phase 1, done):* probed the LED GPIO (PA5) and measured the blink, then probed the telemetry **downlink TX** and used **UART serial decode** to read a heartbeat frame off the wire (115200 8N1), byte-for-byte matching the host decode. Gotcha learned: USART2 is not on the headers (it is the ST-Link VCP, isolated by default), so the scope-able line is **USART6/PC6** - exactly why the downlink UART exists. *Phase 4:* packet period/jitter, signal integrity. *Phase 5:* I2C/SPI waveforms and decode. *Phase 6:* the watchdog reset pulse. *Phase 7:* FOC driver PWM and motor phases. *Learn:* probe compensation, edge triggering, timebase and volts/div, serial decode, measurements.
- **Logic analyzer.** Cheap multi-channel USB digital capture. Complements the scope when several digital lines need decoding at once (bus plus chip-selects).
- **Multimeter.** Continuity, rail voltages, basic current. Always the first sanity check before powering or probing anything.

### Software toolchain

- **STM32CubeIDE.** Eclipse-based, so a clunky editor, but the debugger works out of the box. Its debugger **is `arm-none-eabi-gdb`**, so using it teaches GDB. The flashable image comes from the CMake cross-build (`just obc-build`), not from the IDE - see [setup.md](setup.md).
- **GDB / SWD.** In CubeIDE: breakpoints, step, watch, registers, memory, peripheral view - this *is* GDB. For the raw-CLI experience, drive `arm-none-eabi-gdb` against OpenOCD or the ST-Link GDB server (`target remote :3333`, `monitor reset halt`, `load`, `break main`). SWD is the 2-wire debug transport (SWDIO/SWCLK). Host side: a MinGW gdb debugs the `fsw` doctest binary on the PC, same commands, so it doubles as practice for the target.
- **git + SSH signing.** Run git from Windows (PowerShell 7), not WSL. Commits are SSH-signed and show Verified; Conventional Commits, every commit explicitly authored by Keating.
- **pre-commit + clang-format + ruff.** Auto-format/lint on commit; `just format` runs it on everything, `just hooks` installs it. CI runs the same checks. A failed hook edits the file and aborts - re-stage and commit again.
- **clang-tidy.** Parses like a compiler and flags real bugs (null derefs, unnecessary copies, missing `override`, narrowing) beyond clang-format's pure style. Runs as a CI gate on `fsw`. Not a GCC replacement - it only analyzes, and it lives in CI on Linux where clang finds the headers.
- **ETL (Embedded Template Library).** Fixed-capacity, no-heap STL-like containers, vendored at `vendor/etl`. Reach for an ETL container wherever a heap-using STL container would otherwise go on the target. Currently backs the mode-transition log.
- **Fusion 360.** The printed frame, mounts, and reaction-wheel parts - see the CAD section below.

## Placement rules

Where every object goes, how it must be oriented, and how close it can sit to its neighbours. Lay parts out physically against these before committing to a model. Each item lists the hard conditions and the reason, so when two rules fight you know which to bend.

### Coordinate frame

- **Z** is vertical = the pivot axis = the lazy-susan centre = the motor spin axis. The platform rotates about Z.
- **X-Y** is the platform plane. Take +X as the camera-facing direction so "front" is unambiguous; origin on the pivot axis.

### Global rules

- **Everything fits inside a ~100 mm cube**, antennas included. The lazy-susan is the only thing allowed to overhang, below the bottom plate.
- **Mass balance is the master rule.** The whole rotating assembly's centre of gravity must sit on the Z pivot axis. Off-axis CG puts a standing gravity torque on the platform that the reaction wheel has to fight, and the pointing demo will not work. Place heavy items symmetrically or counterweight opposite. Test it: a balanced platform, nudged, does not always drift back to the same heading.
- **Keep the CG low** where you have the choice, but **on-axis beats low** if they conflict.
- **Boards mount on standoffs, never flush** - air gap, no shorts, no flex.
- **Locking/crimped connectors only** on the spinning rig.
- **Single-point (star) ground** at the supply negative; the OBC<->ESC link needs the shared reference.
- **Three things repel their neighbours:** magnetic fields (motor + encoder magnet) push the IMU away; RF antennas need clear air; heat (ESC, motor, bucks) stays away from the battery.
- **Route the harness clear of the flywheel and rotor** - nothing in the swept volume, and nothing able to flop into it.

**Priority when conditions conflict:** (1) motor + flywheel + encoder coaxial on Z, (2) mass balance, (3) IMU away from magnets and high-current wires, (4) antenna clearance, (5) heat separation, (6) access/convenience.

### Reaction-wheel core

- **Motor (GBM4108-120T):** spin axis exactly on the Z pivot axis, coaxial with the lazy-susan centre within ~0.5 mm and with no tilt - wheel torque has to act about the same axis the platform turns on. Vertical, stator rigidly bolted to the gimbal plate through its bracket, rotor/bell on the flywheel side. Its permanent magnets are the IMU's worst enemy.
- **Flywheel:** coaxial with the motor and the pivot axis, flat, concentric within ~0.25 mm - an off-centre or tilted wheel wobbles and wrecks the balance. Balanced in itself (trim until there is no heavy spot). **Clear swept volume:** a full cylinder around it plus a few mm, with nothing in it. Rim-weighted for inertia, rim kept symmetric.
  - **Sizing it: the platform turns only when wheel torque (tau = I x alpha) clears the bearing's breakaway friction**, so there are two levers, not one. On inertia: **I is proportional to m x r^2**, so radius counts quadratically and mass only linearly - fill every weight pocket **outermost first** (mass near the hub contributes almost nothing), and expanding the radius even 10% buys 1.21x the inertia for the same mass. Keep the mass symmetric: an unbalanced wheel vibrates *and* pushes the CG off-axis, which raises bearing friction and works against you. Weights must be **captive** - a bolt leaving a spinning wheel is a hazard.
  - **Material does not matter for inertia.** The steel weights are the mass; PETG (~1.27) versus PLA (~1.24) is a rounding error and both are ~6x less dense than the bolts. PLA is if anything preferable here - stiffer, so the rim holds shape and retains the weights. Print generous walls around the pockets.
  - **The friction lever is usually the bigger win.** Degrease the lazy-susan and re-lube light: factory grease is thick and dominates stiction. Keep the stack light and the CG centred, since friction scales with normal force and an off-centre load binds the race.
  - **Reaction torque only exists while the wheel's speed is *changing*** (tau = I x alpha). At constant flywheel speed the platform feels nothing but bearing drag, so a steadily-spinning wheel will never rotate the platform continuously - the demo is a **transient**. Step the wheel T0 -> T8 and watch the platform kick one way, T8 -> T0 and it kicks back; oscillating between two speeds makes it rock visibly. Put a marker on the platform edge so a few degrees is legible. (This is also why real spacecraft need momentum dumping: the wheel saturates and magnetorquers shed the accumulated momentum.)
  - **Tether routing dominates a bench test.** Bring the USB and supply leads **up along the spin axis**, never out radially: an axial wire only has to twist, which costs almost no torque, while a radial one has to drag and bend and will mask the effect entirely.
  - **Measure before optimising:** hang a known weight on a string at a known radius and find the force that just starts it turning - breakaway torque = force x radius - then compare against the wheel's achievable I x alpha. That tells you whether you need 20% more inertia or 3x, i.e. whether pocket-filling suffices or the bifilar-suspension fallback is needed.
- **AS5600 + diametric magnet:** magnet on the rotor shaft end, centred on the spin axis within ~0.25-0.5 mm, turning with the rotor, and **diametrically magnetised** - a standard axial disc magnet will not work. The IC fixes to the stator side, centred on the spin axis, facing the magnet across a **0.5-3 mm air gap (aim 1-2 mm)**, sense centre coaxial within ~0.25 mm. Nothing magnetic or ferrous near it but its own magnet. It wires to the ESC, short run.
- **ESC (B-G431B-ESC1):** near the motor so the three phase wires stay short. Give it air - it runs warm. Noise source: keep it and its phase wires away from the IMU, the INA228 sense, and the antennas.
- **Lazy-susan bearing:** under the bottom plate, its axis = Z, centred, platform sitting level. It may be wider than the 100 mm body since it lives below it. Test its stiction by hand early - the #1 mechanical risk. **The fallback if it is too stiff for clean pointing** is a torsion / bifilar suspension: hang the platform from a thin wire so it rotates near-frictionless, which is the standard cheap reaction-wheel-demo pivot. The wire's slight restoring torque is fine for detumble and pointing, and it kills stick-slip entirely.

### Sensors

- **ICM-20948 IMU (the fussy one):** rigid to the plate - it reports the platform's attitude, so no flex or rattle, axes square to the body frame (note the mounted orientation so firmware can remap). **Far from every magnet and high-current wire**: the motor magnets, the AS5600 magnet, the phase wires, and the battery leads all corrupt the magnetometer, and the mag is what corrects gyro drift. Aim for 30-50 mm+ from the motor/magnet and never route motor or battery current past it. Also keep it clear of the ESC and bucks.
- **INA228:** in the power path - its shunt sits inline with the rail it watches, so it goes where the measured current flows. STEMMA QT daisy-chains on to the TMP117, so keep the two within cable reach.
- **TMP117:** placeable - put it where the temperature you care about is (structure for ambient, or near the ESC/motor to watch their heat). Good thermal contact with whatever it measures.
- **ArduCAM (OV2640):** at a frame opening, lens pointing out, **clear unobstructed field of view** - nothing in the cone. Sensor perpendicular to the view; note which way is "up" in the image. Leave room for the lens to screw in and out for focus.

### Comms (Phase 8)

- **RFM95 LoRa:** near an edge/top with the antenna in clear air, running as straight as possible, away from metal (flywheel, battery) and from the other antenna. Fed from the dedicated 3.3 V buck, not the Nucleo 3V3.
- **nRF24L01+PA+LNA:** same antenna-clearance story for its 2.4 GHz whip - the hardest thing to fit inside 100 mm, so plan its space first. **10 uF right at the module's VCC/GND.** 3.3 V from the radio buck.
- **Antennas generally:** clear of metal and of each other; separate the 915 MHz and 2.4 GHz elements. SMA dimensions are standardized, so an antenna mount can be modeled before the part is in hand. The printed frame is plastic (good); the flywheel and battery are not. **Never transmit without an antenna attached.**

### Power and misc

- **Battery (Phase 8):** the heaviest item, so it dominates the balance - place it to bring the rotating CG onto the axis, low if you can. Firmly secured against vibration. Away from ESC/motor heat, accessible to swap and charge, XT60 reachable. Its high-current leads stay away from the IMU and the antennas.
- **Buck converters:** near the power input (short high-current input runs). They get warm and they switch - give them air and keep them away from the IMU, the INA228 sense, and the antennas.
- **WS2812 status LEDs:** visible from outside - the array shows mode/fault/link, so it faces up from the top module's centre, readable at any spin angle.
- **Harness:** locking/crimped connectors, everything routed clear of the flywheel, service loops and strain relief. Motor phase wires and battery leads short, bundled, and away from the IMU and antennas.

### Keep-away matrix

| This... | must stay away from... | because |
| ------- | ---------------------- | ------- |
| IMU (magnetometer) | motor magnets, AS5600 magnet, motor/battery wires, bucks | corrupts the mag heading |
| LoRa antenna | nRF24 antenna, flywheel, battery | detuning / coupling |
| nRF24 antenna | LoRa antenna, metal | detuning |
| Flywheel swept volume | everything | collision |
| Battery | ESC/motor heat | LiPo safety + drift |
| ESC / bucks (noise + heat) | IMU, INA228 sense, antennas, battery | noise + heat |

### Not on the satellite

**Ground LoRa antenna: a soldered wire, not a connector (decided 2026-07-29).** The Feather M0 RFM95 (#3178) ships with no antenna connector, and the board on the bench has no usable uFL or edge-SMA footprint either - checked on the part, not from a drawing. So the antenna is a **quarter-wave wire soldered into the ANT through-hole: 8.2 cm at 915 MHz** (c/f/4; insulated wire runs a few percent short, so 7.8-8.2 cm is the window). One joint, no connector, and the ground box has room for it to stand up straight.

The two ends deliberately do not match, and do not need to: a link needs each end resonant at 915 MHz and properly fed, not identical hardware. The satellite keeps the right-angle SMA stub because it has to lie flat on a spinning plate; the ground box gets the wire because nothing there is space-constrained. A straight quarter-wave is often the better radiator of the two - screw-on stubs are helicals compromised for size.

Caveat if range ever disappoints: a quarter-wave monopole wants a counterpoise, and the Feather's small ground pour is a poor one. Soldering a second 8.2 cm wire to a GND pad pointing the other way makes it a rough dipole, which helps more than swapping antennas would. And per the part constraints above, **solder the wire before the first transmit** - the Feather is the one board here whose antenna is something to remember rather than something already attached.

These live in the 3D-printed ground station, not the spacecraft: the **SSD1306 OLED**, the **second nRF24**, and the **Feather M0 RFM95** - which is both the LoRa radio and the host, so there is no separate host board. A Teensy was the plan until 2026-07-31; the Feather has the radio on it already, leaves SPI free for the nRF24 and I2C free for the display, and is its own USB serial port.

## 3D-printed parts and CAD (Fusion 360 -> `cad/`)

Mechanical models live in `cad/` in the main repo. Parts are hand-modeled in Fusion (parametric where it helps); the Fusion cloud project is the editable master, and **STEP is what the repo tracks** - exported per part and per assembly, with a `_vNN` suffix carrying the revision.

**Design around the hard-fit parts.** The motor (bell/shaft/holes) and the bearing (OD/ID/height) are precise physical interfaces - have both in hand, then model the flywheel, motor bracket, platform, and bearing seat to their real dimensions rather than printing first and hunting for a fit.

### Folder layout

```
cad/
  assemblies/     combined models, one per plate of the stack
    comms/          comms_assembly + rf/
    compute/        compute_assembly + power/ + sensor/
    gimbal/         gimbal_assembly + rw/
    stack_assembly  the whole three-plate stack
  printed/        parts designed here and FDM-printed
  lib/            authored building blocks reused across assemblies (the protoboards)
  hardware/       off-the-shelf fasteners (McMaster imports), one file per size
    inserts/  nuts/  screws_bolts/  standoffs/
  reference/      vendor component CAD - placed for fit, never edited - by subsystem
    actuation/      gimbal motor, ESC
    comms/          LoRa, nRF24, SMA edge connector
    compute/        the Nucleo
    power/          battery, buck
    sensors/        AS5600 encoder + its diametric magnet
```

The assemblies mirror the **three-plate stack** (gimbal / compute / comms), with `stack_assembly` combining them.

**The one rule that decides where a file goes: did I author it?**

- **Authored** (editable, your IP): `assemblies/`, `printed/`, `lib/`.
- **Off-the-shelf** (placed but never edited): `reference/` (vendor models, grouped by subsystem) and `hardware/` (fasteners). The two stay separate on purpose - a screw is not a reaction wheel.

### Printed parts

| Part | What it is |
| ---- | ---------- |
| `gimbal_plate` | the bottom plate - motor, flywheel, encoder, lazy-susan |
| `compute_plate` | the middle plate |
| `compute_mount` | the mount carrying the compute-plate electronics |
| `comms_plate` | the top plate - radios, LED array, switch (Phase 8) |
| `flywheel` | the reaction wheel |
| `driver_cover` | cover over the ESC |
| `buck_cover` | cover over the buck converters |
| `top_cover` | the stack's top cover |

### lib - authored building blocks

The protoboards, modeled once and instanced wherever they appear: **4x6** (power and radio boards) and **7x9** (sensor and stm32 boards). Reuse them rather than copying - fix a dimension once and every assembly updates.

### What is deliberately not modeled

`reference/` carries only the components whose dimensions actually drive the design. Several devices - the IMU, INA228, TMP117, and the ArduCAM - are **intentionally absent**: their fit was confirmed physically on the real protoboard, so modeling them would add work without informing a decision. That is a decision, not a gap. Model a vendor part only when a clearance, a hole pattern, or a keep-out depends on it.

### Hardware / fasteners

**One shared folder, not per-assembly.** Screws, nuts, standoffs, and heat-set inserts live in `hardware/`, one file per unique size, inserted as many occurrences as needed. Duplicating a screw into every assembly is the thing to avoid.

Getting them in: **Insert -> McMaster-Carr Component** is built into Fusion - search the exact metric part, pick STEP, and it drops in at real dimensions. Manufacturer STEP or GrabCAD covers anything McMaster does not list.

Length matters for **standoffs** (they set board-to-board spacing, a real dimension the frame depends on) and for **screws** only where one has to clear a board or bottom out in a standoff. Don't model real threads - they are heavy and slow; a cosmetic thread or plain shaft at the nominal diameter is right, since clearance holes care about the clearance diameter.
