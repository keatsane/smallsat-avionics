/**
 * @file   board.h
 * @brief  NUCLEO-F446RE pin map - the one place board wiring lives
 *
 * Each signal's port/pin/af (alternate function)
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32f446xx.h"

// interrupt priority plan. cortex-m priorities are least-urgent-when-numerically-largest, and
// freertos refuses a FromISR call from any isr more urgent than
// configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5) - it trips configASSERT. everything here that
// talks to the kernel sits at or above that number; 6 leaves 5 free for something that has to be
// more urgent than the uarts and still hand work to a queue
#define IRQ_PRIO_UART 6U

// status led - ld2
#define LD2_PORT GPIOA
#define LD2_PIN  5U

// ws2812 status array - single-wire data on pa8, clocked out by tim1_ch1 (af1) + dma2
#define WS2812_PORT   GPIOA
#define WS2812_PIN    8U
#define WS2812_AF     1U
#define WS2812_TIM    TIM1
#define WS2812_DMA    DMA2
#define WS2812_STREAM DMA2_Stream1
#define WS2812_DMA_CH 6U  // stream 1 channel 6 = tim1_ch1
#define WS2812_COUNT  3U  // 3 beads

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

// actuation - usart1 (tx/rx af7) - stm32g431cb esc
#define ESC_UART   USART1
#define ESC_PORT   GPIOA
#define ESC_TX_PIN 9U
#define ESC_RX_PIN 10U
#define ESC_AF     7U
#define ESC_IRQ    USART1_IRQn

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

// camera - spi3 (sck/miso/mosi af6, cs on pb0 as a plain gpio) - arducam ov2640 fifo
#define CAMERA_SPI      SPI3
#define CAMERA_SPI_PORT GPIOC
#define CAMERA_SCK_PIN  10U
#define CAMERA_MISO_PIN 11U
#define CAMERA_MOSI_PIN 12U
#define CAMERA_SPI_AF   6U
#define CAMERA_CS_PORT  GPIOB
#define CAMERA_CS_PIN   0U

#endif  // BOARD_H
