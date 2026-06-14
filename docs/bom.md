# Bill of materials

Bench hardware that is actually in use. More sensors and actuator parts get added as they come up.

| Item | Qty | Est. cost | Notes |
| ---- | --- | --------- | ----- |
| STM32 Nucleo-F446RE dev board | 1 | $19.00 | The flight computer; onboard ST-Link and USB virtual COM port. |
| Mini-USB cable, data capable | 1 | $8.99 | ST-Link programming and UART-over-USB. |
| Breadboard | 1 | ~$5.00 | Bench wiring. |
| Jumper wires | 1 set | ~$6.00 | Bench wiring. |
| SparkFun ICM-20948 9-DoF IMU breakout | 1 | ~$17.00 | Accel/gyro over SPI, plus the AK09916 magnetometer through the chip's internal I2C master; all three streams are live on the bench. |

Voltage/current, temperature, reaction-wheel parts, and the rest get added here as each piece is brought up on the bench.

## Datasheets and references

Datasheets are vendored in `docs/datasheets/` and linked by relative path, so they keep opening even when vendor pages move around. New devices get their own block here as they are added.

### STM32 Nucleo-F446RE (STM32F446RE)

- [STM32F446RE datasheet](datasheets/stm32f446re-datasheet.pdf) - pinout, alternate-function mappings, electrical specs
- [RM0390 reference manual](datasheets/rm0390-reference-manual.pdf) - register-level detail for every peripheral
- [PM0214 programming manual](datasheets/pm0214-programming-manual.pdf) - Cortex-M4 core: NVIC, SysTick, SCB, instruction set
- [UM1724 user manual](datasheets/um1724-nucleo64-user-manual.pdf) - the Nucleo-64 board: jumpers, ST-Link, pin mapping
- [ES0298 errata](datasheets/es0298-errata.pdf) - silicon limitations and workarounds
- [Board product page (st.com)](https://www.st.com/en/evaluation-tools/nucleo-f446re.html) - schematics (MB1136) under CAD Resources, plus CubeIDE downloads

### ICM-20948 9-DoF IMU (SparkFun breakout)

- [ICM-20948 datasheet (DS-000189)](datasheets/icm-20948-datasheet.pdf) - register map (four banks), SPI/I2C interface formats, electrical specs
- [Product page (invensense.tdk.com)](https://invensense.tdk.com/en-us/products/9-axis/icm-20948) - latest datasheet revisions and app notes
- [Hookup guide (learn.sparkfun.com)](https://learn.sparkfun.com/tutorials/sparkfun-9dof-imu-icm-20948-breakout-hookup-guide) - breakout pinout, onboard 1.8 V regulation + level shifting, jumper notes
