/**
 * @file   ws2812.h
 * @brief  ws2812 status array - stage colors, then clock the frame out over pwm+dma
 */

#ifndef WS2812_H
#define WS2812_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  bring up the pwm transport and blank the array
 */
void ws2812_init(void);

/**
 * @brief  stage one bead's color - lands on the wire at the next ws2812_show()
 * @param  idx  bead index from the mcu end; out of range is ignored
 * @param  r    red, 0-255
 * @param  g    green, 0-255
 * @param  b    blue, 0-255
 */
void ws2812_set(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief  stage every bead off
 */
void ws2812_clear(void);

/**
 * @brief  encode the staged colors and start the burst - skipped if one is still going out
 */
void ws2812_show(void);

/**
 * @brief  is a frame still on the wire
 * @return true until the burst and its latch gap have finished
 */
bool ws2812_busy(void);

#ifdef __cplusplus
}
#endif

#endif  // WS2812_H
