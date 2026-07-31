# Bill of materials

Bench hardware that is actually in use. More sensors and actuator parts get added as they come up.

| Item | Qty | Est. cost | Notes |
| ---- | --- | --------- | ----- |
| STM32 Nucleo-F446RE dev board | 1 | $19.00 | The flight computer; onboard ST-Link and USB virtual COM port. |
| Mini-USB cable, data capable | 1 | $8.99 | ST-Link programming and UART-over-USB. |
| Breadboard | 1 | ~$5.00 | Bench wiring. |
| Jumper wires | 1 set | ~$6.00 | Bench wiring. |
| SparkFun ICM-20948 9-DoF IMU breakout | 1 | ~$17.00 | Accel/gyro over SPI, plus the AK09916 magnetometer through the chip's internal I2C master; all three streams are live on the bench. |
| Adafruit INA228 power monitor breakout (#5832) | 1 | $14.95 | Bus voltage/current/power over I2C (20-bit, 85 V); up on the bench, current verified. |
| Adafruit TMP117 temperature breakout (#4821) | 1 | $11.50 | High-accuracy structural temperature over I2C (16-bit, +-0.1 degC, addr 0x48); on the bench reading ~24 degC. |
| ArduCAM Mini 2MP (OV2640) | 1 | $25.99 | Imaging payload, on two buses: SPI3 for the frame FIFO and I2C1 for register config over SCCB. Both answer on the bench - the chip ID reads back and the SPI test register round-trips. The driver is written (ArduChip FIFO control, non-blocking capture, chunked read-out) but has not been run against the camera yet, and the sensor's JPEG register tables still need vendoring. |
| GBM4108-120T gimbal motor | 1 | ~$40.00 | The reaction-wheel actuator. 24N22P, so 11 pole pairs, 12.4 ohm windings. Runs closed-loop FOC on the bench at ~344 RPM for 0.18 A. |
| B-G431B-ESC1 FOC driver | 1 | $19.42 | Runs SimpleFOC and closes the motor loop locally, reading the encoder itself; takes speed/torque commands from the OBC over UART. |
| AS5600 magnetic encoder + diametric magnet | 1 | ~$10.00 | Rotor position for sensored FOC. Wired to the ESC, not the OBC. Verified at 0x36 with the magnet detected, and one hand revolution reads 6.26 rad against 6.28 theoretical. |
| 4in ball-bearing lazy-susan turntable | 1 | ~$12.00 | The pivot the platform spins on - the wheel's torque only shows if the platform turns nearly friction-free. |
| RFM95W 900 MHz LoRa breakout (Adafruit #3072) | 1 | $19.95 | The low-rate TT&C link, satellite side. Wired onto the shared SPI3 bus with its own CS/DIO0/RST; no driver yet. |
| nRF24L01+PA+LNA 2.4 GHz transceiver | 2 | ~$8.50 | The high-rate payload downlink. They come in pairs, so one is the satellite side on SPI3 with its own CSN/CE/IRQ, and the other is the ground station's receiver. Wants a 10 uF cap right at its VCC or the link browns out. |
| 915 MHz right-angle SMA antenna | 1 | ~$4.00 | Screws onto the edge-launch SMA on the LoRa board; the right angle lets it lie flat on the top plate. |
| 2.4 GHz SMA stub antenna | 1 | ~$3.50 | For the nRF24, which arrived bare. |
| MP1584EN buck converter | 2 | ~$2.00 each | 14.8 V down to 5 V for the Nucleo's E5V pin, and to 3.3 V for the radios. Both adjustable, so both need setting with a meter before anything is connected to them. |
| WS2812 RGB LED bead | 3 | (from the Cyberbrick kit) | Status array on the top plate - mode, fault, and link - chained on one data line off PA8. |
| 470 uF / 50 V electrolytic | 1 | ~$0.70 | Bulk cap across the ESC's power input, soaking motor-PWM switching and regen transients. |
| Ovonic 4S 14.8 V 1550 mAh LiPo | 2 | ~$25.00 each | For running untethered. Not in the loop yet - the bench supply stands in until the fuse is fitted. |
| JST-XH connector kit + ratcheting crimper | 1 | ~$25.00 | Every removable connection in the build. XH latches and seats in 0.1in protoboard; JST-PH does neither, which is why it got dropped partway through. |
| XT60 connectors | 3 pairs | ~$8.00 | The battery and the ESC feed - anything carrying motor current. |
| Protoboard, 0.1in double-sided | assorted | ~$10.00 | The OBC slice, sensor board, power distribution board, LED array, and radio module. |

| Adafruit Feather M0 RFM95 (900 MHz) | 1 | ~$34.95 | The ground station. Chosen over a separate Teensy because the LoRa radio is already on the board, SPI stays free for the nRF24 receiver, I2C is free for the display, and it is its own USB serial port - a Teensy would have meant wiring a second RFM95 to gain nothing. |
| SSD1306 0.96in 128x64 I2C OLED | 1 | ~$6.00 | The ground station's local readout. Picked over an I2C 16x2 LCD for two reasons: those LCD backpacks are 5 V parts whose I2C pull-ups would put 5 V on the Feather's 3.3 V pins, and 128x64 shows mode, faults, link and sequence at once where 2x16 characters cannot. |

The PTC fuse gets added as it actually goes into the build.

## Datasheets and references

Datasheets are vendored in `docs/datasheets/` and linked by relative path, so they keep opening even when vendor pages move around. Files are grouped by subsystem - `compute/`, `sensors/`, `actuation/`, `payload/`, `comms/`, `power/` - and named `<part-or-doc-id>-<type>.pdf`. New devices get their own block here as they are added.

### STM32 Nucleo-F446RE (STM32F446RE)

- [STM32F446RE datasheet](datasheets/compute/stm32f446re-datasheet.pdf) - pinout, alternate-function mappings, electrical specs
- [RM0390 reference manual](datasheets/compute/rm0390-reference-manual.pdf) - register-level detail for every peripheral
- [PM0214 programming manual](datasheets/compute/pm0214-programming-manual.pdf) - Cortex-M4 core: NVIC, SysTick, SCB, instruction set
- [UM1724 user manual](datasheets/compute/um1724-nucleo64-user-manual.pdf) - the Nucleo-64 board: jumpers, ST-Link, pin mapping
- [ES0298 errata](datasheets/compute/es0298-errata.pdf) - silicon limitations and workarounds
- [Board product page (st.com)](https://www.st.com/en/evaluation-tools/nucleo-f446re.html) - schematics (MB1136) under CAD Resources, plus CubeIDE downloads

### ICM-20948 9-DoF IMU (SparkFun breakout)

- [ICM-20948 datasheet (DS-000189)](datasheets/sensors/icm-20948-datasheet.pdf) - register map (four banks), SPI/I2C interface formats, electrical specs
- [AK09916 datasheet](datasheets/sensors/ak09916-datasheet.pdf) - the magnetometer die inside the package, on its own register map
- [Product page (invensense.tdk.com)](https://invensense.tdk.com/en-us/products/9-axis/icm-20948) - latest datasheet revisions and app notes

The magnetometer needs both sheets open at once. It is a separate chip at 0x0C that the ICM-20948 reaches over its own internal I2C master, so its CNTL2 mode byte and the ST1/ST2 status bytes are in the AK09916 sheet, while the slave-0 registers used to drive it are in the ICM-20948 sheet.

### INA228 power monitor (Adafruit #5832)

- [INA228 datasheet (SLYS021)](datasheets/sensors/ina228-datasheet.pdf) - register map, the SHUNT_CAL / current equations, ADC and conversion config, electrical specs
- [Product page (adafruit.com)](https://www.adafruit.com/product/5832) - the breakout: onboard 15 mohm shunt, STEMMA QT, address jumpers, schematic

### TMP117 temperature sensor (Adafruit #4821)

- [TMP117 datasheet](datasheets/sensors/tmp117-datasheet.pdf) - register map, the temperature result format, config and EEPROM registers, electrical specs
- [Product page (adafruit.com)](https://www.adafruit.com/product/4821) - the breakout: STEMMA QT, the ADDR jumper (default 0x48)

### OV2640 camera (ArduCAM Mini 2MP)

- [OV2640 datasheet](datasheets/payload/ov2640-datasheet.pdf) - sensor register map, the SCCB interface, output formats
- [ArduCAM Mini 2MP hardware application note](datasheets/payload/arducam-m-2mp-hardware-app-note.pdf) - the ArduChip register map on the SPI side: capture control, FIFO clear and burst read, the FIFO length registers
- [ArduCAM Mini 2MP user guide](datasheets/payload/arducam-m-2mp-user-guide.pdf) - board pinout, power, and the capture sequence

The module answers on two buses at once: SCCB (I2C1, 0x30) for register config, and SPI3 for reading frames out of the onboard FIFO. Neither can do the other's job, and the documents split the same way - the OV2640 sheet covers the sensor, the application note covers the ArduChip that buffers it. The OV2640 mode init tables are in neither; those only exist in ArduCAM's library source, as `ov2640_regs.h`.

### B-G431B-ESC1 FOC driver

- [UM2516 user manual](datasheets/actuation/um2516-esc1-user-manual.pdf) - board layout, the J1/J3/J4/J8 connector maps, power stage, and which MCU pins land where
- [Board product page (st.com)](https://www.st.com/en/evaluation-tools/b-g431b-esc1.html) - schematics and CAD under the design resources tab

Two things worth knowing before wiring it: the I2C the encoder needs is the **Hall/encoder pads (J8) repurposed** - PB7 for SDA, PB8 for SCL - and J8's supply line is 5 V, so the AS5600's 3.3 V has to come off the J4 SWD pads instead. The serial console is USART2 on PB3/PB4, not the Arduino default.

### AS5600 magnetic encoder

- [AS5600 datasheet](datasheets/actuation/as5600-datasheet.pdf) - register map, the I2C interface at 0x36, and the status bits

It sits on the ESC's bus, not the OBC's, so there is no driver for it in `obc/` - this sheet is for reading it by hand on the bench or for setting SimpleFOC up. The DIR pin picks which way counts up: tied to ground the value increases clockwise, tied to VDD it increases counterclockwise. Magnet placement shows up in the status register rather than as a bad angle - MD means detected, ML means too weak or too far, MH means too strong or too close.

### RFM95W LoRa radio (Adafruit #3072)

- [SX1276/77/78/79 datasheet](datasheets/comms/sx1276-datasheet.pdf) - the transceiver itself: register map, LoRa modem config, FIFO and DIO mapping
- [RFM95/96/97/98(W) module datasheet](datasheets/comms/rfm95-datasheet.pdf) - the HopeRF module built around it: pinout, RF specs, antenna port
- [SX1276 product page (semtech.com)](https://www.semtech.com/products/wireless-rf/lora-connect/sx1276) - errata and application notes
- [Breakout product page (adafruit.com)](https://www.adafruit.com/product/3072) - the breakout: 3.3 V regulator, level shifting, pin labels

Two sheets because they cover different halves. The driver talks to the SX1276, so that is the one with the registers in it; the HopeRF sheet is for pinout and RF only.

### nRF24L01+ transceiver

- [nRF24L01+ product specification](datasheets/comms/nrf24l01p-datasheet.pdf) - register map, Enhanced ShockBurst, and the auto-acknowledge behavior the payload link leans on
- [nRF24 series page (nordicsemi.com)](https://www.nordicsemi.com/Products/nRF24-series) - the family page and the evaluation kit files

It has to be the **plus** specification. The original nRF24L01 sheet is a different part with the same name on the cover: on the +, RF_SETUP bit 5 selects the 250 kbps rate, where the original reserves that bit and requires it written as zero.

### MP1584EN buck converters

- [MP1584 datasheet](datasheets/power/mp1584-datasheet.pdf) - the regulator on the module: 4.5-28 V in, 3 A, 1.5 MHz switching, and the 0.8 V feedback reference

Both modules are the adjustable kind, so the trim pot has to be set with a meter on the output before anything gets wired to it - whatever they ship at is arbitrary. The 14.8 V bus sits comfortably inside the input range. Output is set by the feedback divider working against a 0.8 V reference, so a small turn of the pot moves the rail a long way.

### WS2812B addressable LEDs

- [WS2812B datasheet](datasheets/compute/ws2812b-datasheet.pdf) - the one-wire bit timing, the reset gap, and the byte order

The timing table is basically the whole spec, since the driver is just a pulse train on PA8. The parts that matter: the reset gap between frames is anything above 50 us of low, and each pixel takes 24 bits in green-red-blue order, high bit first - not RGB, which is the easy way to get a display that lights up in the wrong colors.
