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

Linked rather than vendored - the PDFs are large and the vendors keep these URLs stable. New devices get their own block here as they're added.

### STM32 Nucleo-F446RE (STM32F446RE)

- [STM32F446RE datasheet](https://www.st.com/resource/en/datasheet/stm32f446re.pdf) - pinout, alternate-function mappings, electrical specs
- [RM0390 reference manual](https://www.st.com/resource/en/reference_manual/rm0390-stm32f446xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) - register-level detail for every peripheral
- [PM0214 programming manual](https://www.st.com/resource/en/programming_manual/pm0214-stm32-cortexm4-mcus-and-mpus-programming-manual-stmicroelectronics.pdf) - Cortex-M4 core: NVIC, SysTick, SCB, instruction set
- [UM1724 user manual](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf) - the Nucleo-64 board: jumpers, ST-Link, pin mapping
- [ES0298 errata](https://www.st.com/resource/en/errata_sheet/es0298-stm32f446xcxe-device-errata-stmicroelectronics.pdf) - silicon limitations and workarounds
- [Board product page](https://www.st.com/en/evaluation-tools/nucleo-f446re.html) - schematics (MB1136) under CAD Resources, plus CubeIDE downloads

### ICM-20948 9-DoF IMU (SparkFun breakout)

- [ICM-20948 datasheet (DS-000189 v1.3)](https://cdn.sparkfun.com/assets/7/f/e/c/d/DS-000189-ICM-20948-v1.3.pdf) - register map (four banks), SPI/I2C interface formats, electrical specs; opens in the browser (SparkFun's mirror - TDK's own datasheet URLs rot)
- [Product page](https://invensense.tdk.com/en-us/products/9-axis/icm-20948) - latest datasheet revisions and app notes
- [SparkFun hookup guide](https://learn.sparkfun.com/tutorials/sparkfun-9dof-imu-icm-20948-breakout-hookup-guide) - breakout pinout, onboard 1.8 V regulation + level shifting, jumper notes
