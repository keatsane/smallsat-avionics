# Wiring reference and build guide

The bench sheet: what wire goes where. `hardware.md` holds the reasoning, the parts, and the placement rules; `journal.md` holds how any of it went.

**Everything below is built and wired as of 2026-07-26**, including both radios on the shared SPI3 bus and the 3.3 V buck that feeds them. What is left on the hardware is the **PTC fuse and the battery harness**, both of which wait for Phase 8; the bench PSU's current limit stands in for the fuse until then. The radios have no firmware yet, which is a software gap, not a wiring one.

## Things that silently destroy parts - read before touching a board

None of these announce themselves. A chip killed this way looks perfect, measures perfect, and does not work.

- **ESD.** A human feels ~3000 V; a chip dies at ~100 V. Ground yourself before handling a board, work off carpet, handle by the edges. A wrist strap is ~$8.
- **Never solder on a connected board.** Disconnect power, USB, every cable - a grounded board and a hot iron is an ESD path through you.
- **Ungrounded iron tips defeat an ESD-safe station.** With the iron hot, measure AC volts tip to mains earth: near zero is good, tens of millivolts is injecting into every joint.
- **Minimise rework near fine-pitch packages** - use screw terminals or existing connectors, and solder in one pass.
- **Connection order: ground and VCC first, high voltage last.** A chip with no valid ground reference passes current out through whatever pins are at a defined potential, typically the I2C lines.
- **Adafruit INA228 jumper trap:** VBUS goes to **either** VIN+ (back jumper) **or** VIN- (a wire), **never both** - bridged, it shorts the shunt and puts system current through a signal trace. Preferred is the back jumper and no VBUS wire. Read any new breakout's pinout page before wiring it.

## Two rules that decide where caps and grounds go

**Bulk caps go where a device draws current in *bursts*, nowhere else.** A bulk cap is a local charge reservoir: when a device suddenly demands current, wire inductance stops the upstream supply delivering it fast enough, and the cap covers the gap. No burst demand, no cap. Note this is different from the 0.1 uF **decoupling** ceramics sitting at every chip - those are already fitted by the breakout manufacturer and are never something to add.

| Location | Current behaviour | Cap |
| -------- | ----------------- | --- |
| nRF24 | ~115 mA TX bursts | **mandatory 10 uF at its own VCC/GND pins** - the #1 dead-link cause |
| LoRa | ~120 mA on TX | yes, 10 uF at VIN/GND |
| ESC input | motor PWM + regen transients | yes - the 470 uF/50 V |
| Slice 3V3 rail | MCU + sensors, steady | nice-to-have (10 uF fitted) |
| Sensor board | IMU ~4 mA, TMP 0.15 mA, steady | **no** - adding one would be cargo-culting |
| LED array | 20-40 mA dim | optional, skipped |

**Every ground arriving at a board joins one local rail - except where a per-cable return is already the design.** Signals are voltages *referenced to ground*, so a device and its master must share one reference or logic levels are undefined. On the **radio module** the buck's ground and the SPI3 cable's ground must be joined locally, giving the SPI signals a short return right where they are used. On the **sensor board** the grounds were instead left per-cable, each returning to the slice's rail - that is a star ground with the slice as the star point, every signal still has a dedicated adjacent return, and it is verified working. Both are valid; do not rework the sensor board for consistency's sake.

## The boards - where wires actually land

| Board | Mounting | What lands on it |
| ----- | -------- | ---------------- |
| Nucleo slice (**one board, both headers**) | 2x 2x19 female sockets, spanning CN7 and CN10 | everything - I2C1, SPI2, SPI3, all three UARTs, the LED, the radio control lines, and the supply rails |
| Sensor protoboard | screwed to compute plate | IMU, INA228, TMP117, ArduCAM - plus the I2C bus fan-out |
| ESC (B-G431B-ESC1) | friction-clamped | battery in, bulk cap, 3 motor phases, encoder I2C, UART to OBC |
| Buck converters (x2) | friction-clamped | 2 in / 2 out holes each |
| Power distribution protoboard | below the bucks | main power in, fans out to both bucks and the ESC; star ground |
| Gimbal plate | below compute | motor (3 phases), AS5600 (solder direct to its PCB holes) |

**One slice, not two (revised 2026-07-22).** The slice is now a single protoboard carrying both 2x19 female sockets, clipping onto CN7 and CN10 together with open board area between them. This **eliminates the 3V3 + GND bridge** - the rails and the signals are on the same board, so the old CN7-to-CN10 jumper (the most-likely-to-be-forgotten step in the build) no longer exists. Solder both sockets first and check the CN7-to-CN10 spacing seats without racking before committing all 76 pins.

**Three rails span the board**, and everything taps them: **3V3** (from CN7-16), **GND** (from CN10-9, with CN10-10/20 and a CN7 ground tied into the same rail), and the **SPI3 bus** (PC10/PC11/PC12 as single nodes, since the camera *and* both radios share it - never run separate wires per device back to the morpho pins). E5V (CN7-6) is separate: 5 V in from the buck, feeding nothing else on the board.

**The slice is a routing layer - the connector does not go where the pins are.** The morpho socket brings signals up at scattered positions (the IMU's four are CN10-30/28/26/16, not adjacent). Do not put the connector there. Instead: pick any clean row of N holes, solder the N-pin XH header there, and run **short board jumpers** from each scattered morpho pin to its header pin. The board gathers the scattered signals into one tidy header, so it stays **one connector per device** no matter how spread out its pins are - never split a device across two connectors. Power/ground pins do not route back to a specific morpho pin at all: they tap the nearest **3V3 or GND rail** hole, so only the signal pins need a jumper. (IMU example: 5-pin header, GND to the local rail, then SCK->CN10-30, MISO->CN10-28, MOSI->CN10-26, CS->CN10-16.)

**Connectors are JST-XH (2.5 mm), not PH.** XH latches (holds against vibration, releases with a tab press), its terminals are big enough to crimp reliably, and at 2.5 mm it seats in standard 2.54 mm protoboard - PH at 2.0 mm does not. One ratcheting tool crimps both, so the existing PH connectors at the ESC are not wasted. Use loose crimp terminals with a ratcheting crimper, never solder to pre-tinned pigtail leads (the crimp-backout failure mode).

## Power chain

```text
SOURCE (bench PSU for bring-up; 4S pack later)
  (+) --[PTC fuse]--[rocker switch]--> POWER DISTRIBUTION BOARD   (fuse + switch both Phase 8)
                                                  |
                              +-------------------+-------------------+
                              |                   |                   |
                          5V buck IN         3.3V buck IN         ESC VIN+
                              |                   |                   |
                          5V buck OUT        3.3V buck OUT       [470uF 50V cap
                              |                   |               across VIN+/GND,
                     Nucleo E5V (CN7-6)     radio 3V3 rail        at the terminals,
                              |             (LoRa + nRF24)        can anchored]
                    Nucleo onboard 3V3 reg
                              |
                     3V3 (CN7-16) --> slice 3V3 rail --> sensors
  (-) ------------------------------- STAR GROUND on the distribution board
```

Order on the positive lead is **fuse first (nearest the source), then switch**. The fuse protects the maximum length of wire that way.

### The distribution board itself

Two rails: a **+V rail** (main ~14.8 V) and the **star ground** - every ground in the satellite comes home here, none daisy-chained device to device.

| In / out | Wire | Endpoint | Connector | Colour |
| -------- | ---- | -------- | --------- | ------ |
| in | main + | PSU+/battery+ **via inline fuse** -> +V rail | XT60 (PSU clips for now) | red |
| in | main - | source- -> star ground | XT60 | black |
| tap | +V -> 5 V buck IN+ | on-board | solder | red |
| tap | +V -> 3.3 V buck IN+ | on-board | solder | red |
| tap | +V -> ESC feed + | to ESC | XT60 | red |
| tap | star -> each buck IN- and ESC feed - | on-board / XT60 | - | black |
| out | 5 V buck OUT+ -> Nucleo E5V | slice, CN7 end | JST-XH 2 | **red, banded 5V** |
| out | 5 V buck OUT- -> Nucleo GND | slice, CN7 end | same XH2 | black |
| out | 3.3 V buck OUT | radio protoboard 3.3 V rail | JST-XH 2 | red, black |

**Set each buck's output with a meter BEFORE any load is connected** - a buck trimmed to 12 V into the Nucleo E5V destroys it. Power the board from the PSU (14.8 V, limit ~200 mA), trim the 5 V buck to 5.0 V and confirm the 3.3 V buck reads 3.3 V, all with their outputs open. Verify main+ to star-ground reads **open** (no short) before ever applying battery power.

**Fuse: deferred to the Phase-8 battery harness, bare main + lead until then.** Blade fuses are the wrong scale here (bulky inline holders, loose spade fit). The selected part is a **Littelfuse RUEF300** PTC - 30 V, 3 A hold, radial through-hole, soldered in series with main+, auto-resetting. 3 A clears the ~1-2 A load and spin-up without nuisance-tripping, trips ~6 A, 100 A interrupt. Bourns MF-R300 and Littelfuse 30R300 are equivalents. **Not yet ordered** - see Carried work in `roadmap.md`. The bench PSU's current limit is the only protection until it is fitted, and **it must be fitted before the LiPo ever runs**.

**Switch and battery are accounted for, not built yet:** the switch is a series element in the main + lead (`battery+ -> fuse -> switch -> board`), inserted later by cutting that lead onto the switch tabs - leave slack. The main-bus INA228 relocation (sensor board to inline on this board's input, for total pack draw) is also Phase 8.

**Nucleo power, staged.** Keep the Nucleo on **USB (jumper on U5V)** while bringing up sensors - fewer variables, and you need USB for the console anyway. Only move the jumper to **E5V** and feed CN7-6 from the 5 V buck once you are deliberately testing the full power chain. The jumper selects one or the other; it will not power from USB while set to E5V.

**With JP5 on U5V, external 5 V on CN7-6 is ignored** - the buck reads a healthy 4.9 V while the MCU stays dark, and LD1 keeps blinking because the ST-LINK runs off USB regardless. **LD2 blinking green is the only "MCU is running" indicator**, not LD1. Measure at CN7-6 itself, not at the cable end.

**Suspect the newest cable before the silicon.** Three multi-hour debugs on this build were marginal cables, and the component tests came back clean every time.

**Every device gets its ground on the same connector as its power.** A powered device whose ground arrives on a signal connector floats when that connector is out, and hunts for a return through its I/O pins - on a shared bus that is enough to take a *different* device off the air. Cost a day when the camera did it to the INA228 (2026-07-26, mechanism found 2026-07-30; see `journal.md`).

**Crimp 24-26 AWG on the power connectors**, not 30 - XH terminals are rated 22-30 and grip poorly at the thin end. Check each crimp grips conductor and insulation, tug-test it, and confirm it clicks into its housing.

## Signal connections

Every pin below is the live map in `obc/Inc/board.h`; morpho holes are verified in the Board and rail reference at the end of this file.

### The ten connectors on the slice

**Built and verified 2026-07-22.** Both sockets seated, three rails run, all ten headers soldered, ~35 jumpers landed, the 470 ohm and 10 uF fitted. Verified: 3V3 rail reads 3.3 V, no rail-to-rail shorts, board boots on USB. (The optional 0.47 uF was skipped - the Nucleo's regulator is already well bypassed and the 10 uF covers bulk.)

**Physical layout - two columns, in numeric order top to bottom:**

```text
   [ CN7 socket ]                          [ CN10 socket ]
        left column                          right column
        (CN7-fed)                            (CN10-fed)
          3  camera SPI3                       1  sensor pwr+I2C
          7  5V in                             2  IMU SPI2
          8  radio SPI3 bus                    4  ESC UART
          9  LoRa control                      5  HIL UART
         10  nRF control                       6  LED
```

Each connector sits on the side of the board fed by its own socket, which keeps every jumper short.

Signals gather to each header via short board jumpers; power and ground tap the nearest rail. Max 5 pins, so 2/3/4/5-way XH stock covers everything with nothing to split. **(9) and (10) share the same wire colours - label them.**

Pins read **`<MCU pin>/<Morpho hole>`**, and the Morpho hole always carries its `CN7-` or `CN10-` prefix - the two headers both number into the 30s, so a bare number is ambiguous (PA9 is CN10-21 *and* PB7 is CN7-21).

| # | Connector | Wires | Pins | Colours |
| - | --------- | ----- | ---- | ------- |
| 1 | Sensor power+I2C (XH4) | 3V3, GND, SDA, SCL | rail, rail, PB9/CN10-5, PB8/CN10-3 | red, black, blue, yellow |
| 2 | IMU SPI2 (XH5) | GND, SCK, MISO, MOSI, CS | rail, PB13/CN10-30, PB14/CN10-28, PB15/CN10-26, PB12/CN10-16 | black, yellow, green, blue, white |
| 3 | Camera SPI3 (XH5) | GND, SCK, MISO, MOSI, CS | rail, bus, bus, bus, PB0/CN7-34 | black, yellow, green, blue, white |
| 4 | ESC UART (XH3) | TX, RX, GND | PA9/CN10-21, PA10/CN10-33, rail | blue, green, black |
| 5 | HIL UART (XH3) | TX, RX, GND | PC6/CN10-4, PC7/CN10-19, rail | blue, green, black |
| 6 | LED (XH3) | +5V, GND, DIN | CN7-18, rail, PA8/CN10-23 | red, black, blue |
| 7 | 5V in (XH2) | E5V, GND | CN7-6, rail | **red banded**, black |
| 8 | Radio SPI3 bus (XH4) | GND, SCK, MISO, MOSI | rail, bus, bus, bus | black, yellow, green, blue |
| 9 | LoRa control (XH3) | CS, DIO0, RST | PB7/CN7-21, PA0/CN7-28, PA1/CN7-30 | white, green, blue |
| 10 | nRF control (XH3) | CSN, CE, IRQ | PA4/CN7-32, PC2/CN7-35, PC3/CN7-37 | white, green, blue |

Rows 1-7 cross-check against the signal table below. **Rows 9 and 10 (the radios) do not** - they are Phase 8 and were never added to that table, so their Morpho holes are inferred from the CN7 grouping (PC2/CN7-35 and PC3/CN7-37 are confirmed in JOURNAL 2026-07-22). Verify them against the board before crimping.

**Components on the slice:** a **330-470 ohm resistor in series on the DIN line** of connector 6 (WS2812 data protection - put it here so the LED board gets a clean signal), and optionally a **1-10 uF cap across the 3V3/GND rails** for local decoupling. Nothing else: no pull-ups (the breakouts carry them), no level shifters (all 3.3 V), no transistors or diodes.

**Not on this slice:** radio 3.3 V power (that comes from the radio buck, which is why 8/9/10 carry no red wire). The LED's supply *does* come from here, but off **+5V at CN7-18**, not the 3V3 rail - three different power sources on this rig, do not mix them.

| Signal | MCU pin | Morpho hole | Slice | Goes to |
| ------ | ------- | ----------- | ----- | ------- |
| I2C1 SCL | PB8 | CN10-3 | CN10 | sensor protoboard (bus fans out on-board) |
| I2C1 SDA | PB9 | CN10-5 | CN10 | sensor protoboard |
| SPI2 SCK (IMU) | PB13 | CN10-30 | CN10 | sensor protoboard -> IMU |
| SPI2 MISO (IMU) | PB14 | CN10-28 | CN10 | sensor protoboard -> IMU |
| SPI2 MOSI (IMU) | PB15 | CN10-26 | CN10 | sensor protoboard -> IMU |
| IMU CS | PB12 | CN10-16 | CN10 | sensor protoboard -> IMU |
| SPI3 SCK (camera) | PC10 | CN7-1 | **CN7** | sensor protoboard -> ArduCAM |
| SPI3 MISO (camera) | PC11 | CN7-2 | **CN7** | sensor protoboard -> ArduCAM |
| SPI3 MOSI (camera) | PC12 | CN7-3 | **CN7** | sensor protoboard -> ArduCAM |
| Camera CS | PB0 | CN7-34 | **CN7** | sensor protoboard -> ArduCAM |
| USART1 TX (ESC) | PA9 | CN10-21 | CN10 | ESC **RX** |
| USART1 RX (ESC) | PA10 | CN10-33 | CN10 | ESC **TX** |
| USART6 TX (HIL) | PC6 | CN10-4 | CN10 | USB-serial adapter RX (optional this session) |
| USART6 RX (HIL) | PC7 | CN10-19 | CN10 | USB-serial adapter TX (optional) |
| 3V3 out | - | CN7-16 | CN7 | feeds the slice 3V3 rail -> sensors |
| E5V in | - | CN7-6 | CN7 | from the 5 V buck (only when off USB) |
| GND | - | CN10-9/10/20, CN7 grounds | both | star ground |

**The camera splits across both slices** - its SPI3 and CS come off CN7, while its I2C config (SCCB) rides the shared I2C1 bus that arrives on CN10. That is not an error in the table.

**Cross TX to RX on the ESC link.** Two TX pins wired together is the classic dead-serial-link bug.

**USART2 (PA2/PA3) needs no wiring** - it is the ST-Link virtual COM port over the USB cable.

## The sensor protoboard - three connectors, and the bus fans out on-board

The I2C bus arrives **once** and is distributed on the board. Do not run separate I2C to the camera.

| Connector | Wires | Feeds |
| --------- | ----- | ----- |
| 1. Power + I2C (JST-XH 4) | 3V3, GND, SDA, SCL | splits 4 ways for power, **3 ways for I2C** - INA228 (0x40), TMP117 (0x48), and the camera's SCCB (0x30) all share the one bus |
| 2. IMU SPI2 (JST-XH 5) | GND, SCK, MISO, MOSI, CS | the IMU |
| 3. Camera SPI3 (JST-XH 5) | GND, SCK, MISO, MOSI, CS | the ArduCAM |
| a. INA shunt (JST-XH 2) | VIN+, VIN- | **to the power board**, inline with main battery + (XH is rated 3 A against a <1.5 A peak) |

**Device pin details** (labels vary by breakout - these match the boards in use, and the IMU/INA/TMP mappings are proven by the working firmware):

- **IMU (SparkFun ICM-20948):** in SPI mode the I2C-looking labels change role - **SCL = SCK, SDA = MOSI, AD0 = MISO**, plus CS. VIN and GND to the rails. **Tie FSYNC to GND**; leave INT (data-ready, unused since the driver polls) and AUX_DA/AUX_CL (the internal path to the AK09916 magnetometer) unconnected.
- **TMP117:** VIN, GND, SDA, SCL to the rails. **INT** is a hardware limit-alert output - unused, the driver polls. ADDR unconnected defaults to 0x48.
- **INA228:** VIN/GND/SDA/SCL to the rails; VIN+ and VIN- out to the power board. **VBUS** is the bus-voltage sense pin and on this breakout it is isolated, connected to nothing - **wire it to VIN- locally on the sensor board** (the load-side tap, per the datasheet's high-side circuit). It never runs to the power board. Left floating, the chip measures a floating pin and reports nonsense, which is what tripped the bench UNDERVOLTAGE before the jumper was fitted. One VBUS-to-VIN- wire bypasses nothing - VBUS is a high-impedance input. ALRT unconnected. **VIN+, VIN- and VBUS all showing continuity is healthy**, not a short: the 15 mohm shunt reads as a dead short on any meter.
- **ArduCAM OV2640: all 8 pins are needed** - I2C configures the sensor, SPI reads the JPEG out of the FIFO, and neither does the other's job. Five leave the board on connector 3 (GND + the four SPI lines); VCC, SDA, SCL tap the board rails, **and so does a second GND** (added 2026-07-30, see the ground rule above). It is the only device fed from two runs, so it is the only one that can be powered without a reference - both grounds are deliberate.

Connectors 1 and 2 run to the **CN10 side** of the slice; connector 3 runs to the **CN7 side**.

**Chain the I2C sensors with STEMMA QT** - one JST-SH cable from the board's I2C fan-out into the INA228, a second from the INA228's other port to the TMP117. Power and I2C both ride the chain, so the TMP117 needs no separate wiring. The camera's SCCB taps the same bus from the protoboard's own holes.

**The INA228 measures the main battery bus (decided 2026-07-22).** The chip stays soldered on the sensor protoboard - it sits at the corner nearest the power distribution board, ~1 inch away - but its **VIN+ / VIN- shunt leads run to the power board, inline with the main battery positive**, after the fuse and switch:

```text
BATTERY+ --[PTC fuse]--[switch]--> INA VIN+ --[shunt]-- INA VIN- --> +V rail --> bucks + ESC
```

This works because the INA228's measurement front end handles up to 85 V **independently of its 3.3 V logic supply** - the chip runs at 3.3 V on the sensor board while its shunt terminals sit at battery potential (standard high-side sensing). Everything downstream of the shunt is measured, so bus voltage reads **actual battery state-of-charge** and current reads **total system draw** - which is what makes `UNDERVOLTAGE -> SAFE` a real demonstration rather than a theoretical one.

Earlier plans had the shunt inline with the 3V3 logic rail, which would have read a rock-stable 3.3 V that can never sag and ~75 mA of sensor draw - valid telemetry, but not the spacecraft power model. The objection to routing battery current near the magnetometer still holds in general; it is acceptable here only because the run is ~1 inch, at the board corner, with the IMU at the opposite (top, centred) end. **Use the heaviest wire on hand for the two shunt leads** - they carry full system current (~1-2 A, higher on motor spin-up); 26 AWG is acceptable at that length, 24 AWG or thicker preferred. The sensor board's own 3V3 rail is fed **directly** from connector 1, not through the shunt.

**Firmware follow-up:** the over/undervoltage thresholds are currently sized for a 3.3 V rail and must move to 4S LiPo values - ~16.8 V full charge, ~13.6 V undervoltage (3.4 V/cell), ~17.0 V overvoltage. Same logic, different constants. SHUNT_CAL is unchanged (same onboard 15 mohm shunt).

## Motor domain - ESC and gimbal plate

| Link | Wires | Layer | Connector |
| ---- | ----- | ----- | --------- |
| Distribution -> ESC power | 2: VIN+, GND | intra (compute) | **XT60** (on hand) |
| Bulk cap -> ESC | across VIN+/GND | intra | soldered at the terminals, can anchored |
| ESC -> motor | 3: phase A, B, C | **inter** (compute -> gimbal) | direct solder + heat-shrink, or individual spades |
| ESC <-> AS5600 | 4: 3V3, GND, SDA, SCL | **inter** (compute -> gimbal) | JST-XH 4; solder direct into the AS5600's PCB holes at the far end |
| ESC <-> OBC | 3: TX, RX, GND | intra (compute) | JST-XH 3 |

**The AS5600 wires to the ESC, not the STM32.** The ESC closes the FOC loop locally and needs the encoder; the OBC only sends speed/torque commands over UART. This is the most common point of confusion in this build.

**The ESC's I2C is the Hall/encoder pads (J8), repurposed - there is no dedicated I2C connector.** On the B-G431B-ESC1 the Hall inputs map to PB6 (H1), PB7 (H2), PB8 (H3), and SimpleFOC uses **PB7 = SDA, PB8 = SCL** (`PIN_WIRE_SDA=PB7`, `PIN_WIRE_SCL=PB8`); H1/PB6 goes unused. On most revisions these are bare solder pads, not a plug.

**J8 pad order (confirmed against the user manual):** GND, 5V, Z+/H3, B+/H2, A+/H1. So SDA lands on **B+/H2**, SCL on **Z+/H3**, GND on the GND pad, and A+/H1 goes unused.

**The AS5600 breakout has no regulator**, and its R2/R3 are already the 10k I2C pull-ups - **add none**. In parallel with the ESC's ~10k on the Hall lines that gives ~5k effective, near the textbook 4.7k; more would over-pull the bus.

**Power the AS5600 from the ESC's J4 SWD pads (3.3 V), not J8's 5 V** - J8's supply is 5 V, and a 5 V-referenced pull-up puts 5 V on a 3.3 V part. **J4 pad order (UM2516 Table 6): 1 = SWDIO, 2 = SWCLK, 3 = MCU VDD (3V3), 4 = GND**, so 3V3 and GND are the two pads at one end, GND outermost. The manual does not say how pin 1 is marked, so **find orientation by continuity, not by probing voltage**: beep from any large ground to each end pad; the end that beeps is pad 4/GND and 3V3 is next to it. Backwards means soldering VCC onto SWDIO. The 3V3 rail is live whenever the board is powered, and the AS5600's ~6.5 mA is nothing to it. The encoder harness therefore lands on **two** ESC connectors: VCC on J4, SDA/SCL/GND on J8.

Once powered, SDA/SCL must idle at ~3.3 V - near 5 V means the Hall pull-ups reference 5 V and wants sorting first. Validate on an external supply, not USB alone; USB-only reports incorrect velocity.

**Confirm the J8 pad order against UM2516 or the board schematic before soldering** - pad order varies by revision.

Cut the motor's long phase leads down to a tidy length, but keep the encoder's 4-wire run **physically separated from the three phase wires**. Phase lines are switched PWM and AS5600 I2C is noise-sensitive.

### AS5600 checks

**Verified 2026-07-21** - STATUS masked 0x20, AGC 36, I2C scan found 0x36, one hand rotation read 6.26 rad against 6.28 theoretical.

**Air gap is tuned by AGC, not by eye.** Read register 0x1A: at 3.3 V the range is 0-128 and the target is **60-70** (low means too close, high too far). Move the magnet ~0.25 mm at a time, staying inside the 0.5-3 mm spec. Register 0x0B (STATUS) carries the magnet bits - **mask with 0x38**, since bits 0-2 and 6-7 read as garbage; **0x20** is the ideal result.

**Standalone bench check**, if the encoder ever needs re-verifying off the ESC: VCC-GND open and DIR-GND beeping unpowered, SDA-VCC and SCL-VCC each ~10k; ~5-10 mA draw at 3.3 V with SDA/SCL idling at 3.3 V and DIR at 0 V; then probe the analog `OUT` pin while rotating the magnet a full turn - it must sweep smoothly and wrap exactly once. That last test covers the chip, the joints, the magnet and the gap together.

**Trim all solder tails flush.** A screw is a floating conductor; the moment a second tail touches it, it bridges two nets.

## The ground station box

One Feather M0 RFM95 plus two modules. The LoRa radio is already on the Feather, so nothing is
wired for it - only the nRF24 and the display need wires, and the nRF24 shares the SPI bus the
LoRa is already using. Colours follow the same role scheme as the satellite.

**Already spoken for by the onboard LoRa - do not reuse:** pin 8 (its CS), pin 4 (RST), pin 3
(DIO0), and SCK/MOSI/MISO. Also leave **pin 9** alone: it is A7, the battery-voltage divider.

| Module | Signal | Feather pin | Colour |
| ------ | ------ | ----------- | ------ |
| nRF24 | VCC | 3V3 (**never 5 V**) | red |
| nRF24 | GND | GND | black |
| nRF24 | SCK | SCK | yellow |
| nRF24 | MOSI | MOSI | blue |
| nRF24 | MISO | MISO | green |
| nRF24 | CSN | 10 | white |
| nRF24 | CE | 11 | blue |
| nRF24 | IRQ | not connected | - |
| OLED | VCC | 3V3 | red |
| OLED | GND | GND | black |
| OLED | SDA | SDA | blue |
| OLED | SCL | SCL | yellow |

**The nRF24 needs a 10 uF cap across its own VCC and GND pins**, as close to the module as it
goes - the same rule as the satellite side, and `bom.md` names it the number one dead-link cause.
It draws ~45 mA receiving and pulls hard on transients; the Feather's regulator can supply it,
but not through a length of thin wire without local storage.

**IRQ is deliberately unconnected.** The driver polls, so the line buys nothing yet. Wire it to
pin 6 if a future version wants to sleep waiting for a packet.

**Add no I2C pull-ups.** SSD1306 modules carry their own, and the Feather has none - which is the
right combination. A second I2C device later inherits those, it does not need more.

**Both antennas on before power.** The nRF24 has its SMA stub; the LoRa needs a 78 mm wire on the
Feather's ANT pad (a quarter wave at 915 MHz). Transmitting without one can damage a PA, and the
box has two radios that can transmit.

**Power budget, all four loads receiving:** SAMD21 ~10 mA, RFM95 ~12, nRF24 PA+LNA ~45, SSD1306
~20 - about **87 mA against the Feather's 600 mA regulator and a 500 mA USB budget**. Not close
to a limit, which is why USB alone runs the whole box.

26 AWG, same as the satellite harness. The runs are short enough that gauge is about handling
rather than drop.

## Intra-layer vs inter-layer at a glance

**Inter-layer (must unplug to separate plates):**
- compute -> gimbal: 3 motor phase bullets + the encoder JST-XH 4
- compute -> comms: LED JST-XH 3, then the radio connectors (all Phase 8)

**Everything else is intra-layer**, on the compute plate: the slices, sensor protoboard, ESC, both bucks, and the distribution board.

## Harness bundling and routing

There are no printed wire channels except the one in the gimbal section that keeps the motor phases clear of the flywheel. That is fine - channels are not what makes a harness tidy.

**Ties:** hook-and-loop (Velcro) cable ties while building and debugging - reusable, undone by hand, no cutting. For the final tidy-up, **waxed lacing cord** spot-tied every few cm: it is what real spacecraft and aircraft harnesses use, sits far lower-profile than any tie, does not crush insulation, and comes off by snipping one loop. Spiral wrap is a reasonable middle option on the inter-plate runs. Zip ties are rejected - bulky, they bite insulation, and servicing means cutting them off.

**Tie-down points:** the inter-plate **standoffs**. No reprinting needed - lace or Velcro each bundle to a standoff.

**Routing habit:** give each domain a side. Motor and power hug the ESC/power side of the plate; signals hug the sensor/OBC side.

| Bundle | Wires | Notes |
| ------ | ----- | ----- |
| A - sensor board <-> CN10 side | power+I2C (4) + IMU SPI2 (5) | fine together: all 3.3 V logic, same destination, short run |
| B - sensor board <-> CN7 side | camera SPI3 (5) | own bundle, other socket |
| C - OBC <-> ESC UART | TX, RX, GND (3) | own small bundle |
| D - distribution -> ESC power | VIN+, GND (2) | twist the pair, keep off the signal side |
| E - motor phases | A, B, C (3) | twist all three, isolated; this is the one in the gimbal channel |
| F - encoder -> ESC | 3V3, GND, SDA, SCL (4) | bundle together, route separately from E |

**The hard rule: never bundle the motor phases (E) with anything, least of all the encoder (F).** The phases are switched PWM carrying amps; AS5600 I2C is weak, slow, open-drain, and exactly what that corrupts. The ESC power pair (D) stays off the signal side too.

- **Twist each conductor with its return.** Power pairs (+/-) and the three motor phases twist so the currents cancel. Signals return in ground, so ground rides in the same bundle - the encoder's four wires twist as **one** bundle, never split into a VCC+GND pair and an SDA+SCL pair, which leaves both signals with no nearby return.
- **Bundling same-bus wires together is good**, not merely tolerable: one shared return, minimum loop area.
- **Cross noisy and signal bundles at 90 degrees, never parallel** - this matters more than separation, which is scarce inside 100 mm.
- **Service loop at every connector**, bundle anchored a couple of cm back from it so vibration works the anchor and not the joint, and nothing able to flop into the flywheel's swept volume.

## Build order

The build is complete (2026-07-26). Four rules from it that apply to any rework:

- **Signals before power.** Signal work runs on USB with no LiPo and no motor; the power chain is only needed once the motor wants VBAT.
- **Bring up sensors one at a time** and confirm each answers on the console before wiring the next. Chasing one bad joint is easy, chasing four is not.
- **Verify every rail with a meter before connecting a load**, PSU current limit low (~300 mA). This is the step that catches a reversed rail before it kills a board.
- **First power-on always runs off the bench PSU, never the LiPo.** A current-limited supply turns a wiring mistake into a shrug; a 100C pack turns it into smoke.

## The comms plate

Built 2026-07-26. Wiring is done; the drivers are not.

- **LED array:** 3 WS2812 beads chained on their small protoboard (D1 -> D0 down the row), one **JST-XH 3** (+5V, GND, DIN on **PA8 / CN10-23**) crossing up from the CN10 side. Series resistor 330-470 ohm on the first D0. **Powered at 5 V (CN7-18), with 3.3 V data** - the beads are spec'd 3.5-5.3 V, so a 3.3 V supply is below minimum and they simply do not light (found the hard way 2026-07-26). 3.3 V data into a 5 V-powered bead is the intended arrangement and needs no level shifter; if it ever turns flaky, a series diode on the 5 V feed drops it to ~4.4 V and pulls the input threshold down with it. **Bead 0 is the one nearest the data connector** - MODE, then FAULT, then LINK down the chain.
- **Radios:** built as a self-contained module on their protoboard - a shared **SPI3** bus (SCK/MISO/MOSI each one node, wired to both radios) plus a 3.3V and GND rail. Power (3.3V+GND) comes from the **radio buck**, signals from the **CN7 side** - two different destinations, so the down-going pigtails split by destination: power XH2 (to buck), SPI3 bus XH4 (SCK/MISO/MOSI/GND to CN7), LoRa control XH3 (CS/DIO0/RST), nRF control XH3 (CSN/CE/IRQ).

  | RFM95 LoRa (3.3V-only board) | -> | nRF24L01+PA+LNA | -> |
  | --- | --- | --- | --- |
  | VIN -> 3.3V rail | | VCC -> 3.3V rail (**10 uF cap right here**) | |
  | GND -> GND rail | | GND -> GND rail | |
  | SCK/MISO/MOSI -> shared bus | | SCK/MOSI/MISO -> shared bus | |
  | CS -> PB7 | | CSN -> PA4 | |
  | G0 (DIO0) -> PA0 | | CE -> PC2 | |
  | RST -> PA1 | | IRQ -> PC3 | |
  | EN -> tie 3.3V (always on) | | | |
  | G1-G5 -> unconnected | | | |

  The nRF24 10 uF cap at its VCC/GND is the #1 dead-link fix; add one at the LoRa VIN too. Never key EN or transmit without the antenna attached.

## Pin reservations - do not consume these

| Pin | Hole | Reserved for |
| --- | ---- | ------------ |
| PA8 | CN10-23 | WS2812 LED data |
| PB7 | CN7-21 | radio control line |
| PA0 | CN7-28 | radio control line |
| PA1 | CN7-30 | radio control line |
| PA4 | CN7-32 | nRF24 CSN |
| PC2 | CN7-35 | nRF24 CE |
| PC3 | CN7-37 | nRF24 IRQ |

**Shortfall resolved (2026-07-21).** The two radios need six GPIOs - LoRa (CS PB7, DIO0 PA0, RST PA1) and nRF24 (CSN PA4, CE PC2, IRQ PC3) - and the original reserved block only had four spares once **PB0 (CN7-34) went to the camera CS**. **PC2 (CN7-35) and PC3 (CN7-37)** close the gap: both free, both plain GPIO, both confirmed on CN7. Still avoid PA13/PA14 (SWD), PB11 (not bonded out on the LQFP64), and PB3 if SWO trace is ever wanted.

## Wire colors

Read the scheme by **role**, not by signal name, and every one-off line places itself with no new colours and no repeats inside a single cable.

| Colour | Role | Covers |
| ------ | ---- | ------ |
| red | power | 3V3, 5V (**band any 5 V wire** - feeding it to a 3.3 V part is fatal) |
| black | ground | GND |
| yellow | clock | SCK, SCL |
| blue | out (MCU drives) | MOSI, SDA, UART TX, WS2812 DIN, radio RST, nRF24 CE |
| green | in (MCU reads) | MISO, UART RX, LoRa DIO0, nRF24 IRQ |
| white | select | CS, CSN |

**26 AWG throughout on the slice** - no exceptions. Even the E5V feed (the Nucleo's full draw) and the 3V3 bridge (all of CN10's sensor current) are well within it.

## Things that are NOT connected (common confusions)

- The **AS5600 does not touch the STM32** - it is the ESC's encoder.
- The **console UART needs no wires** - it is the ST-Link VCP over USB.
- The **bulk cap lives only at the ESC** - not on the sensor board, not on another plate.
- The **3.3 V buck powers only the radios** - never the Nucleo, never the sensors.
- **No I2C pull-up resistors to add** - the breakouts carry their own.
- **No level shifters, transistors, or flyback diodes anywhere** - everything is 3.3 V logic and the ESC handles the motor internally.
- The **camera needs no separate I2C run** - it taps the shared bus on the sensor protoboard.

## Board and rail reference

Physical holes are ST morpho positions (CN7 / CN10), verified against UM1724 (Table 19 Arduino, Table 29 morpho, Figure 24 board pinout) and the STM32F446 datasheet DS10693 Table 11 (alternate-function map). The live pins also exist in `obc/Inc/board.h`, the firmware's pin-map header.

### Rails

One rail, bused - never one MCU pin per device, and never a GPIO as a supply.

- **3V3 sources:** CN7-16 (morpho) or CN6-4 (Arduino power header). Pick one, run it to a 3V3 rail on the slice, tap every 3V3 part off that rail.
- **5V sources:** CN7-18 (morpho) or CN6-5 (Arduino).
- **Do not mistake these for 5V outputs:** **E5V (CN7-6) is an input** - it powers the whole board from an external 5 V supply. **U5V (CN10-8)** is the 5 V coming from the ST-Link side of the USB. Neither is a general-purpose 5 V tap; use +5V (CN7-18 / CN6-5).
- **Ground:** CN10-9 / CN10-10 / CN10-20 sit right next to the sensors - use one as the GND rail and tie it to the CN7 ground so everything shares one reference.
- **Never power a sensor from a GPIO held high.** A GPIO sources only a few mA, the whole port shares a current ceiling, and the voltage sags under load. Power comes from the rails; GPIOs carry logic only.

### Current budget

The digital sensors are a rounding error on the Nucleo's 3V3 (the onboard regulator path supplies a few hundred mA after the MCU's share):

| Rail | Part | Typical draw |
| ---- | ---- | ------------ |
| 3V3 | ICM-20948 (accel+gyro+mag) | ~3-4 mA |
| 3V3 | INA228 | ~1 mA |
| 3V3 | TMP117 | ~150 uA |
| +5V (CN7-18) | WS2812 status LEDs (3 beads, chained) | ~60 mA per bead at full white; run dim (~10-20%), so a few mA each |

The radios are the real load - the nRF24 PA+LNA pulls ~115 mA in bursts, LoRa ~120 mA on TX. They run off the dedicated battery-fed 3.3 V buck, never the Nucleo's 3V3.

**The motor is not on these rails.** The ESC and motor run off the battery (14.8-16.8 V) - never the Nucleo's 3V3 or 5V. Only two things cross between the OBC and the ESC: the UART link and a shared ground reference.

### Don't-touch and gotchas

- **PA5 is the LD2 LED**, so SPI1's default pins (PA5/PA6/PA7) are out. That is why the IMU is on SPI2 and the camera/radios on SPI3, not SPI1.
- **PB11 is not bonded out on the F446RE** (LQFP64) - Figure 24 shows CN10-18 as NC even though the shared morpho table lists PB11. USART3 on PB10/PB11 is a dead end.
- **PA13 / PA14 are SWD** (ST-Link debug) - leave them alone or you lose flashing and debug.
- **PA2 / PA3 are the USART2 VCP** - they reach the laptop over USB but are isolated from the header pins by default solder bridges (UM1724). Use USART6 (PC6/PC7) for anything you need to probe on a scope or analyser.
- **PB3 is SWO** - avoid it if trace output is ever wanted.
