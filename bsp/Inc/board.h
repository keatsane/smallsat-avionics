/**
 * @file   board.h
 * @brief  NUCLEO-F446RE pin map - the one place board wiring lives
 *
 * Each signal's port/pin/af (alternate function)
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32f446xx.h"

// status led - ld2
#define LED_PORT GPIOA
#define LED_PIN  5U

// imu - spi2 (sck/miso/mosi af5, cs driven as a plain gpio)
#define IMU_SPI      SPI2
#define IMU_SPI_PORT GPIOB
#define IMU_SCK_PIN  13U
#define IMU_MISO_PIN 14U
#define IMU_MOSI_PIN 15U
#define IMU_SPI_AF   5U
#define IMU_CS_PORT  GPIOB
#define IMU_CS_PIN   12U

// sensors - i2c1 (scl/sda af4) - ina228 + tmp117 share the bus
#define SENSOR_I2C      I2C1
#define SENSOR_I2C_PORT GPIOB
#define I2C_SCL_PIN     8U
#define I2C_SDA_PIN     9U
#define I2C_AF          4U

// console - usart2 (af7) -> st-link vcp over usb
#define CONSOLE_UART   USART2
#define CONSOLE_PORT   GPIOA
#define CONSOLE_TX_PIN 2U
#define CONSOLE_RX_PIN 3U
#define CONSOLE_AF     7U
#define CONSOLE_IRQ    USART2_IRQn

// downlink - usart6 (af8) -> header link
#define DOWNLINK_UART   USART6
#define DOWNLINK_PORT   GPIOC
#define DOWNLINK_TX_PIN 6U
#define DOWNLINK_RX_PIN 7U
#define DOWNLINK_AF     8U
#define DOWNLINK_IRQ    USART6_IRQn

#endif  // BOARD_H
