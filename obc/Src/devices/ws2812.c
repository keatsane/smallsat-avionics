/**
 * @file   ws2812.c
 * @brief  ws2812 driver - grb bit encoding on top of the pwm+dma transport
 */

#include "devices/ws2812.h"

#include "board.h"
#include "drivers/pwm_dma.h"

// no clock line - the pulse width is the bit
#define WS2812_PERIOD_TICKS 225U  // 1.25 us at 180 mhz
#define WS2812_T0_TICKS     72U   // 0.4 us -> logical 0
#define WS2812_T1_TICKS     144U  // 0.8 us -> logical 1
#define WS2812_LATCH_SLOTS  224U  // ~280 us of low - the v5 beads want far more than the old 50 us

#define WS2812_BITS_PER_BEAD 24U
#define WS2812_FRAME_LEN     ((WS2812_COUNT * WS2812_BITS_PER_BEAD) + WS2812_LATCH_SLOTS)

static uint8_t rgb[WS2812_COUNT][3];
static uint16_t frame[WS2812_FRAME_LEN];  // static - dma reads it after show() returns

// msb first, one duty value per bit
static void encode_byte(uint8_t v, uint16_t* out) {
    for (uint32_t i = 0U; i < 8U; i++) {
        out[i] = (((uint32_t)v << i) & 0x80U) ? WS2812_T1_TICKS : WS2812_T0_TICKS;
    }
}

void ws2812_init(void) {
    pwm_dma_init(WS2812_PERIOD_TICKS);
    ws2812_clear();
    ws2812_show();  // beads power up showing garbage - blank them now, init takes a while
}

void ws2812_set(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    if (idx >= WS2812_COUNT) {
        return;
    }
    rgb[idx][0] = r;
    rgb[idx][1] = g;
    rgb[idx][2] = b;
}

void ws2812_clear(void) {
    for (uint32_t i = 0U; i < WS2812_COUNT; i++) {
        rgb[i][0] = 0U;
        rgb[i][1] = 0U;
        rgb[i][2] = 0U;
    }
}

void ws2812_show(void) {
    if (pwm_dma_busy()) {
        return;  // mid-frame - reconfiguring now would truncate it
    }

    uint16_t* p = frame;
    for (uint32_t i = 0U; i < WS2812_COUNT; i++) {
        encode_byte(rgb[i][1], p);  // grb on the wire, not rgb
        p += 8U;
        encode_byte(rgb[i][0], p);
        p += 8U;
        encode_byte(rgb[i][2], p);
        p += 8U;
    }

    while (p < &frame[WS2812_FRAME_LEN]) {
        *p++ = 0U;  // low tail
    }

    pwm_dma_send(frame, WS2812_FRAME_LEN);
}

bool ws2812_busy(void) { return pwm_dma_busy(); }
