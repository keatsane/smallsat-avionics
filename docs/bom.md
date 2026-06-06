# Bill of Materials

The bench hardware on hand right now. More sensors and actuator parts get added as the work reaches them.

| Item | Qty | Est. cost | Notes |
| ---- | --- | --------- | ----- |
| STM32 Nucleo-F446RE dev board | 1 | $19.00 | The flight computer; onboard ST-Link and USB virtual COM port. |
| Mini-USB cable, data capable | 1 | $8.99 | ST-Link programming and UART-over-USB. |
| Breadboard | 1 | ~$5.00 | Bench wiring. |
| Jumper wires | 1 set | ~$6.00 | Bench wiring. |
| Siglent SDS804X HD oscilloscope | 1 | owned | 4-ch 12-bit scope for signal integrity, serial decode, and timing. |
| SN65HVD230 CAN transceiver | 2 | $4.00 ea | On hand for later CAN work. |
| 120 ohm CAN termination resistor | 2 | $0.10 ea | On hand for later CAN work. |

The sensors (IMU, voltage/current, temperature), the reaction-wheel parts, and the rest get added here as they're bought.

## Datasheets and references

Linked rather than vendored - the PDFs are large and the vendors keep these URLs stable. New devices get their own block here as they're added.

### STM32 Nucleo-F446RE (STM32F446RE)

- [STM32F446RE datasheet](https://www.st.com/resource/en/datasheet/stm32f446re.pdf) - pinout, alternate-function mappings, electrical specs
- [RM0390 reference manual](https://www.st.com/resource/en/reference_manual/rm0390-stm32f446xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) - register-level detail for every peripheral
- [PM0214 programming manual](https://www.st.com/resource/en/programming_manual/pm0214-stm32-cortexm4-mcus-and-mpus-programming-manual-stmicroelectronics.pdf) - Cortex-M4 core: NVIC, SysTick, SCB, instruction set
- [UM1724 user manual](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf) - the Nucleo-64 board: jumpers, ST-Link, pin mapping
- [ES0298 errata](https://www.st.com/resource/en/errata_sheet/es0298-stm32f446xcxe-device-errata-stmicroelectronics.pdf) - silicon limitations and workarounds
- [Board product page](https://www.st.com/en/evaluation-tools/nucleo-f446re.html) - schematics (MB1136) under CAD Resources, plus CubeIDE downloads

### SN65HVD230 CAN transceiver

- [SN65HVD230 datasheet](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf) - bus levels, slope control, electrical specs
- [TI product page](https://www.ti.com/product/SN65HVD230) - application notes and CAD models

### Siglent SDS804X HD oscilloscope

- [Siglent oscilloscope documents](https://siglentna.com/resources/documents/digital-oscilloscopes/) - user manual, datasheet, quick-start, and serial-decode notes for the SDS800X HD series
