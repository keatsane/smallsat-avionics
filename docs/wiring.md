# Wiring reference and build guide

The bench sheet. hardware.md holds the reasoning, the parts, and the placement rules - this file is "what wire goes where, in what order", so nothing has to be reconstructed mid-solder.

## Scope of this build session

**In:** power distribution (fuse, switch, distribution board, both bucks), the Nucleo and its morpho slice, the sensor protoboard (IMU + INA228 + TMP117 + camera), the ESC with its bulk cap, the motor phases, and the AS5600 encoder.

**Deferred:** the radios. The rest of the comms plate is done - the switch is wired (freestanding, not yet mounted) so the rig powers on and off without cycling the pack connector, and the **LED array is live** (2026-07-26), driven from PA8 by TIM1_CH1 + DMA with all three beads addressable.

## Things that silently destroy parts - read before touching a board

None of these announce themselves. A chip killed this way looks perfect, measures perfect, and simply does not work - exactly what happened to the first INA228, whose cause was never established after fifteen eliminated hypotheses.

**ESD (the invisible one).** A human feels a static discharge at ~3000 V; **a chip dies at ~100 V**. You will never know it happened. Touch a grounded metal object before handling any board, work off carpet, avoid synthetic clothing, handle boards by the edges, keep them in anti-static bags. An anti-static wrist strap is ~$8 and is the single highest-value item on the bench.

**Soldering on a connected board.** Disconnect *everything* first - power, USB, every cable. A board with one grounded point and a hot iron is an ESD path straight through whatever you are touching.

**Ungrounded iron tips.** An ESD-safe station only protects if the tip is actually grounded through its seating; a loose or poorly-seated tip defeats it entirely. Measure **AC volts from tip to mains earth** with the iron hot - a good station reads near zero, tens of millivolts means it is injecting into every joint you make.

**Heat near the chip.** Repeated rework beside a fine-pitch package thermally stresses its joints and the die. **Use screw terminals or existing connectors wherever the board provides them**, and get any soldering done in one pass rather than reworking the same area repeatedly.

**Connection order - ground and VCC first, high voltage last.** A chip with high voltage on its inputs and a floating or intermittent ground has no valid reference, and current finds its way out through whatever pins *are* at a defined potential - typically the I2C lines. This is why intermittent connectors are a hazard to silicon, not just an annoyance.

**Board-specific jumper traps.** On the Adafruit INA228, VBUS must connect to **either** VIN+ (via the back jumper) **or** VIN- (via a wire) - **never both**. Bridged to both, VBUS shorts across the shunt and puts full system current through a signal-sized trace beside the chip. **Preferred: close the back VBUS-to-VIN+ jumper and run no VBUS wire at all** - Adafruit's intended configuration, one less hand-soldered joint near the chip, and it makes the mistake structurally impossible. Read the vendor's own pinout/jumper page before wiring any new breakout; this hazard is documented and easy to miss.

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
                          5V buck OUT        (leave unwired      [470uF 50V cap
                              |               this session)       across VIN+/GND,
                     Nucleo E5V (CN7-6)                           at the terminals,
                              |                                   can anchored]
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
| out | 3.3 V buck OUT | **bare - Phase 8** | none yet | - |

**Set each buck's output with a meter BEFORE any load is connected** - a buck trimmed to 12 V into the Nucleo E5V destroys it. Power the board from the PSU (14.8 V, limit ~200 mA), trim the 5 V buck to 5.0 V and confirm the 3.3 V buck reads 3.3 V, all with their outputs open. Verify main+ to star-ground reads **open** (no short) before ever applying battery power.

**Fuse form factor (decided 2026-07-21):** automotive blade fuses are the wrong scale for this build - the molded inline holders are too bulky, and the spade-terminal workaround is a loose fit that does not inspire confidence. **The fuse is deferred to the Phase-8 battery harness**, where it belongs anyway (in the open lead near the pack, not crammed in the stack) and where the PSU's current limit no longer stands in as protection. Build the board now with a **bare main + input lead**. When the battery goes in, fit a **PTC resettable fuse** - selected part **Littelfuse RUEF300** (30 V, 3 A hold, radial through-hole; solders into the board in series with main+, no holder/blade, auto-resets). Its 3 A hold clears the ~1-2 A load and spin-up without nuisance-tripping and trips ~6 A, and its **100 A interrupt rating** comfortably covers realistic pack fault current. **Not yet ordered - open Phase-8 buy.** Amazon PPTC stock is flaky (multiple 30V/3A listings came up unavailable), so DigiKey (RUEF300, guaranteed stock; the 17-week figure is manufacturer lead time, not shelf stock) is the reliable source - grab qty 5-10 with the next DigiKey order so shipping is already covered, since the part is not needed until the battery harness. Bourns MF-R300 / Littelfuse 30R300 are identical equivalents if an in-stock one turns up first. Alternative: a small inline 5x20 mm glass/ceramic holder with a fast-blow fuse. The bench PSU's current limit is the fault protection until then. The fuse is non-negotiable before the LiPo ever runs - only its form and timing are flexible.

**Switch and battery are accounted for, not built yet:** the switch is a series element in the main + lead (`battery+ -> fuse -> switch -> board`), inserted later by cutting that lead and landing the ends on the switch tabs - leave slack. The fuse is deferred with it (see the fuse note above), landing inline nearest the source end when the battery harness is built. The main-bus INA228 relocation (from the sensor board to inline on this board's input, for total pack draw) is also a Phase-8 change.

**Leave the 3.3 V buck completely unwired this session** - nothing uses it until the radios arrive at Phase 8, and an unterminated live rail is just a short waiting to happen.

**Nucleo power, staged.** Keep the Nucleo on **USB (jumper on U5V)** while bringing up sensors - fewer variables, and you need USB for the console anyway. Only move the jumper to **E5V** and feed CN7-6 from the 5 V buck once you are deliberately testing the full power chain. The jumper selects one or the other; it will not power from USB while set to E5V.

**Gotcha that cost an hour (2026-07-26):** with JP5 left on U5V, external 5 V on CN7-6 is simply ignored - the buck reads a healthy 4.9 V at the connector while the MCU stays dark. LD1 keeps blinking because the ST-LINK is powered from USB regardless, which makes it look like the board is alive. **The definitive "MCU is running" indicator is LD2 blinking green** (the firmware's 1 Hz heartbeat), not LD1. And measure at **CN7-6 itself**, not at the cable end - voltage at the connector proves the buck works, not that it arrives.

**Marginal cables caused three separate multi-hour debugs on this build** - the switch loop reading open under load, the camera's SPI cable whose bad connector *also* stopped the INA228 acknowledging on I2C, and the 3.3 V buck feed collapsing the rail to 1.1 V with no short present. **When a symptom is confusing, suspect the most recently made cable before suspecting silicon.** In every case the electrical tests on the components came back clean, because the components were fine.

**Connector reliability (2026-07-26):** intermittent JST-XH joints on the power path caused several hours of phantom faults - continuity passing on the bench, then dropping out when the board was tilted. Root cause is crimping **30 AWG into XH terminals**, which are rated 22-30 AWG and grip poorly at the thin end. **Use 24-26 AWG on the power connectors**, check each crimp grips both conductor and insulation, tug-test every terminal, and confirm each clicks into its housing (a seated terminal cannot be pushed back out from the front). An intermittent connector on a rig that vibrates will fail in service and present as a firmware bug.

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
- **INA228:** VIN/GND/SDA/SCL to the rails; VIN+ and VIN- out to the power board. **VBUS** is the bus-voltage sense pin, and on this breakout it is **an isolated pin connected to nothing** (verified 2026-07-26: with no jumper fitted it reads open to both VIN+ and VIN-). **It must be wired to VIN- locally on the sensor board** - the load-side tap, matching the INA228 datasheet's high-side application circuit. It never needs a wire to the power board.

**This explains the "floating-bus" UNDERVOLTAGE seen on the bench:** an unconnected VBUS makes the chip measure a floating pin and report nonsense, which tripped the fault. Bus voltage was never actually being measured until this jumper was fitted.

A single VBUS-to-VIN- wire creates **no bypass around the shunt** - VBUS feeds only a high-impedance measurement input, so no current flows through it; current still goes VIN+ -> shunt -> VIN-. (What *would* be harmful is VBUS being tied to both sides, which parallels the shunt with a ~0 ohm path: current measurement collapses to zero and system current is routed through a signal-sized trace.)

**Expect VIN+, VIN- and VBUS to all show continuity with each other** once wired - the 15 mohm shunt reads as a dead short on any multimeter. That is healthy, not a fault. ALRT unconnected.
- **ArduCAM OV2640: all 8 pins are needed.** It is a two-bus device - **I2C configures** the sensor's registers (resolution, format, exposure) and **SPI reads the JPEG** out of the FIFO; neither can do the other's job. Only 5 leave the board on connector 3 (GND + the four SPI lines); VCC, SDA, SCL tap the board rails.

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

**The AS5600 breakout has no regulator** (one IC, plus C1/C2 and R1-R4: R1 = 0 ohm link, R2/R3 = 10k - the I2C pull-ups - and R4 = 1k). R2/R3 are the SDA/SCL pull-ups, so **add none** - in parallel with the ESC's reported ~10k on the Hall lines that gives ~5k effective, essentially the textbook 4.7k, and more would over-pull the bus so the AS5600 struggles to drive it low. Diagnostic once powered: SDA/SCL should idle at ~**3.3 V**; if they idle near 5 V the ESC's Hall pull-ups reference 5 V, which puts 5 V on a 3.3 V-powered AS5600 and wants sorting before a long run. Its VCC feeds the chip more or less directly: **take 3.3 V from the ESC's J4 SWD pads, not J8's 5 V.** That is safe on every AS5600 variant and keeps the whole bus at 3.3 V matching the G431's logic. **J4 pad order (UM2516 Table 6): 1 = SWDIO, 2 = SWCLK, 3 = MCU VDD (3V3), 4 = GND** - so 3V3 and GND are the two pads at one end of the row, GND outermost. The manual's "if the daughterboard is removed" caveat applies to the SWD *signals*, not the power pins; the 3V3 rail is live whenever the board is powered. The manual does not document how pin 1 is marked, so establish orientation with a **continuity test** rather than a voltage probe: beep from any large ground (J8 GND, battery negative, a mounting hole) to each end pad - the end that beeps is pad 4/GND, and 3V3 is the pad next to it. Getting this backwards means soldering VCC onto SWDIO, which back-drives an MCU pin and cannot source the sensor anyway. The AS5600's ~6.5 mA is nothing to that rail. Note the encoder harness therefore lands on **two** ESC connectors: VCC on J4, SDA/SCL/GND on J8.

**J8's supply line is 5 V, not 3.3 V.** Either power the AS5600 from it *if* the breakout has an onboard regulator (check for a SOT-23 near VCC, and check where its I2C pull-ups tie - pull-ups referenced to 5 V put 5 V on the STM32's pins), or source 3.3 V from the ESC's SWD header to keep the whole bus at 3.3 V. Community experience: J8's built-in 10k pull-ups are marginal for I2C, and external **4.7k** fixes a flaky bus. Also, readings taken with the board on USB power alone report incorrect velocity - use external supply when validating.

**Confirm the J8 pad order against UM2516 or the board schematic before soldering** - pad order varies by revision.

Cut the motor's long phase leads down to a tidy length, but keep the encoder's 4-wire run **physically separated from the three phase wires**. Phase lines are switched PWM and AS5600 I2C is noise-sensitive.

### Bench-checking the AS5600 before it meets the ESC

Verify the encoder standalone - it is far easier to debug on its own than wired into the ESC.

**Unpowered (multimeter):** VCC-GND must read open, not a short (a near-0 ohm reading is a solder bridge). DIR-GND must beep, confirming the jumper. SDA-VCC and SCL-VCC should each read **~10k**, which positively confirms R2/R3 are the pull-ups. Check every adjacent pin pair for unexpected bridges. Then beep each pad to the far end of its wire - confirms no broken conductor and verifies which wire is which at the ESC end, since colour alone is not proof.

**Powered (bench PSU, 3.3 V, current limit ~50 mA):** draw should be **~5-10 mA** (limit-slamming means a short, ~0 mA means nothing is connected). SDA and SCL idle at **~3.3 V**; DIR reads **0 V**.

**Functional test, no I2C master needed:** the AS5600's `OUT` pin gives an analog voltage proportional to angle by default. Probe it and rotate the magnet through a full turn - the voltage must sweep **smoothly and monotonically, wrapping exactly once per revolution**. Endpoints do not matter (the default range does not reach the rails); smoothness and a single wrap do. Erratic jumps, dead zones, or multiple wraps point at the magnet - wrong magnetisation, off-centre, or air gap out of range. This one test validates the chip, the solder joints, the magnet, and the gap together.

**Tuning the air gap with AGC (do this, don't eyeball it).** Read register 0x1A over I2C: at 3.3 V the range is 0-128 and you want **60-70**. Low AGC means the magnet is too close, high means too far. With an adjustable magnet stem, move it ~0.25 mm at a time and re-read, keeping total gap inside the 0.5-3 mm spec and MD set / ML+MH clear at every step. Register 0x0B (STATUS) carries those bits - mask it with **0x38**, since bits 0-2 and 6-7 are reserved and read as garbage; **0x20** is the ideal masked result.

**Bring-up result (2026-07-21):** STATUS 0x67 (masked 0x20 - magnet detected, gain not railed), AGC 36 (valid, slightly close), SDA/SCL idle 3.3 V, I2C scan found 0x36, and one full hand rotation gave 6.26 rad against a theoretical 6.28. Encoder subsystem verified end to end.

**Trim all solder tails flush.** A long tail resting against a mounting screw is not shorting anything *yet*, but the screw is a floating conductor - the moment a second tail or wire touches it, it bridges two nets. On a vibrating rig that is a latent fault that appears weeks later.

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

**The hard rule: never bundle the motor phases (E) with anything, and never with the encoder (F).** The phases are switched PWM carrying amps and they radiate; AS5600 I2C is weak, slow, open-drain, and exactly what that noise corrupts. The ESC power pair (D) stays off the signal side for the same reason.

Bundling same-bus wires together is actively good, not merely tolerable - one bus shares a return path, so keeping its wires together (with their own GND in the bundle) minimises loop area and makes them quieter.

Three habits do most of the work:

- **Twist power pairs** (+/-) and **twist the three motor phases**. In both cases the currents largely cancel, so the bundle radiates far less.
- **Cross noisy and signal bundles at 90 degrees, never parallel.** Perpendicular crossings barely couple; parallel runs do. This matters more than raw separation, which is scarce inside 100 mm.
- **Every bundle carries its own ground wire** - never let a signal find its return through some other path.
- **Twist each conductor with its return path.** For a DC power pair the current goes out the + and back the -, so those two are the loop: twist them. For signals the return flows in **ground**, so each signal wants ground adjacent. The encoder's four wires therefore twist as **one bundle** (GND stays near both I2C lines) - do *not* split them into a VCC+GND pair and an SDA+SCL pair, which feels natural but is the worst case: it leaves both signals with no nearby return and couples SCL's edges straight into SDA.

Mechanically: a gentle service loop at each connector so it unplugs without tension, the bundle anchored a couple of cm *back* from the connector so vibration works the anchor and not the solder joint, and nothing that can ever flop into the flywheel's swept volume.

## Build order

Actual sequence, revised to the bottom-up order the build followed. **Stage 0 (gimbal layer) is complete and functionally verified (2026-07-21):** AS5600 proven over USB, motor running closed-loop FOC off the bench PSU at up to ~344 RPM, and the **reaction-wheel effect demonstrated** - stepping the wheel T0 -> T8 -> T0 kicks the platform each way. The smooth commutation also retroactively clears the odd in-circuit phase-resistance reading (14/14/28 ohm): it was the ESC's output stage in parallel, as suspected, since three healthy windings are a precondition for smooth FOC.

The remaining order is chosen by dependency and risk: signal work runs on USB power with no LiPo and no motor, so it comes before the power chain, which is only needed once VBAT is required for the motor.

1. ~~Slice onto the Nucleo~~ **DONE 2026-07-22** - one board spanning CN7+CN10 (no bridge needed), three rails, verified 3.3 V, no shorts, clean boot.
2. ~~XH headers onto the slice~~ **DONE** - all ten, two columns in numeric order (see layout above).
3. **Sensor protoboard connectors**, then bring up sensors **one at a time** on the console - I2C first (INA228, then TMP117 on the chain), then the IMU on SPI2, then the camera. Confirm each answers before wiring the next; chasing one bad joint is easy, chasing four is not.
4. **ESC <-> OBC UART**, TX to RX, verified with a serial round-trip.
5. **Power chain, unpowered:** fuse -> switch -> distribution board, then buck inputs and the 5 V output. Leave the 3.3 V buck entirely unwired. Solder the switch wires to its spade tabs and heat-shrink them.
6. **Verify rails with a multimeter before connecting any load.** Feed the distribution board from the PSU with the current limit low (~300 mA); confirm 5 V reads 5 V and nothing is warm. **Do not skip this** - it is the step that catches a reversed rail before it kills a board.
7. **Move the Nucleo to E5V** and test the full chain on the switch.
8. **ESC VBAT + the bulk cap** across VIN+/GND, can anchored - then motor bring-up, which also settles whether the odd in-circuit phase-resistance reading was just the ESC output stage.

**First power-on runs off the bench PSU with the current limit set low, not the LiPo.** A current-limited supply turns a wiring mistake into a shrug; a 100C pack turns it into smoke.

## Deferred - how the comms plate comes in later

Nothing about today's wiring blocks it. When the top plate is built:

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
- The **3.3 V buck powers nothing** until the radios exist.
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

The radios are the real load and they are Phase 8 - the nRF24 PA+LNA pulls ~115 mA in bursts, LoRa ~120 mA on TX. They get the dedicated battery-fed 3.3 V buck, never the Nucleo's 3V3.

**The motor is not on these rails.** The ESC and motor run off the battery (14.8-16.8 V) - never the Nucleo's 3V3 or 5V. Only two things cross between the OBC and the ESC: the UART link and a shared ground reference.

### Don't-touch and gotchas

- **PA5 is the LD2 LED**, so SPI1's default pins (PA5/PA6/PA7) are out. That is why the IMU is on SPI2 and the camera/radios on SPI3, not SPI1.
- **PB11 is not bonded out on the F446RE** (LQFP64) - Figure 24 shows CN10-18 as NC even though the shared morpho table lists PB11. USART3 on PB10/PB11 is a dead end.
- **PA13 / PA14 are SWD** (ST-Link debug) - leave them alone or you lose flashing and debug.
- **PA2 / PA3 are the USART2 VCP** - they reach the laptop over USB but are isolated from the header pins by default solder bridges (UM1724). Use USART6 (PC6/PC7) for anything you need to probe on a scope or analyser.
- **PB3 is SWO** - avoid it if trace output is ever wanted.
