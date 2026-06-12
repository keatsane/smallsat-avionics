# Bill of Materials

The bench hardware on hand right now. More sensors and actuator parts get added as the work reaches them.

| Item | Qty | Est. cost | Notes |
| ---- | --- | --------- | ----- |
| STM32 Nucleo-F446RE dev board | 1 | $19.00 | The flight computer; onboard ST-Link and USB virtual COM port. |
| Mini-USB cable, data capable | 1 | $8.99 | ST-Link programming and UART-over-USB. |
| Breadboard | 1 | ~$5.00 | Bench wiring. |
| Jumper wires | 1 set | ~$6.00 | Bench wiring. |
| SparkFun ICM-20948 9-DoF IMU breakout | 1 | ~$17.00 | Accelerometer + gyro, read over SPI - both live on the bench. The package also carries a magnetometer, unused for now (it sits behind the chip's internal I2C master, not on the SPI bus). |

The remaining sensors (voltage/current, temperature), the reaction-wheel parts, and the rest get added here as they arrive.

## Datasheets and references

Vendored in `docs/datasheets/` and linked by relative path, so the references always open instead of force-downloading and never rot with the vendors' URLs. New devices get their own block here as they're added.

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
