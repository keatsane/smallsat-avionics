/**
 * @file   ov2640.h
 * @brief  arducam mini 2mp driver - ov2640 sensor over sccb, frame fifo over spi
 */

#ifndef OV2640_H
#define OV2640_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// where a capture is in its life. the fifo holds the frame, so the obc never has to keep up with
// the sensor - it starts a capture, polls, then drains at whatever rate it likes
typedef enum {
    OV2640_IDLE = 0,   // nothing in flight
    OV2640_CAPTURING,  // the arduchip is filling the fifo
    OV2640_READY,      // a whole frame is sitting in the fifo
    OV2640_FAILED,     // the capture did not finish inside the timeout
} ov2640_state_t;

// one health reading, shaped like the other device drivers' samples
typedef struct {
    uint32_t t_ms;
    uint32_t frame_bytes;  // bytes waiting in the fifo, 0 when there is no frame
    ov2640_state_t state;
    bool valid;        // the arduchip answered its test register on this read
    bool frame_ready;  // a frame is ready to drain
} ov2640_sample_t;

/**
 * @brief  bring up the camera - arduchip link, sensor id, then jpeg configuration
 * @return true if the whole chain came up
 */
bool ov2640_init(void);

/**
 * @brief  start a capture - the arduchip fills the fifo on its own from here
 * @return true if a capture started, false if one is in flight or the chip is mute
 */
bool ov2640_capture_start(void);

// the output sizes the driver can be switched between, smallest first. the ids are the wire values
// carried by CAPTURE_IMAGE's argument, so their order is part of the ground interface
typedef enum {
    OV2640_RES_320x240 = 0,
    OV2640_RES_800x600 = 1,
    OV2640_RES_1600x1200 = 2,
    OV2640_RES_COUNT
} ov2640_res_t;

/**
 * @brief  switch the sensor's output size
 * @param  res  which size
 * @return false on an unknown size, or if the sccb writes did not take
 *
 * Rewrites one vendored table, which is all a size change is. Refused while a capture is in
 * flight: the arduchip is latching a frame whose dimensions the sensor is being asked to change.
 */
bool ov2640_set_resolution(ov2640_res_t res);

/** @brief the size the next capture will use */
ov2640_res_t ov2640_resolution(void);

/**
 * @brief  put the read pointer back at the start of the frame already in the fifo
 * @return false if there is no frame to rewind to
 *
 * Reading the fifo moves a pointer; it does not consume anything, and the frame stays put until
 * the next capture overwrites it. So the same image can be downlinked more than once, which is how
 * a lossy one-way link eventually delivers all of it.
 */
bool ov2640_rewind(void);

/**
 * @brief  advance the capture state machine - call once per control cycle
 * @param  t_ms  platform time, for the capture timeout
 * @return the state after this poll
 */
ov2640_state_t ov2640_poll(uint32_t t_ms);

/**
 * @brief  read this cycle's health, for the fsw's dropout detection
 * @return the sample
 */
ov2640_sample_t ov2640_read(void);

/**
 * @brief  bytes of the captured frame still waiting, 0 when there is no frame
 *
 * pure state - unlike ov2640_read() it touches no spi, so it is safe to call between chunks of
 * an open burst read
 */
uint32_t ov2640_frame_bytes(void);

/**
 * @brief  true when a whole frame is sitting in the fifo, ready to drain (touches no spi)
 */
bool ov2640_frame_ready(void);

/**
 * @brief  copy the next chunk of the captured frame out of the fifo
 * @param  buf  destination
 * @param  n    how many bytes to take
 * @return bytes actually copied - short means the frame ended
 *
 * only does anything in OV2640_READY. the read pointer advances across calls, so one frame comes
 * out over as many calls as the caller wants - which is what makes a multi-pass downlink possible
 */
size_t ov2640_read_chunk(uint8_t* buf, size_t n);

/**
 * @brief  drop the frame in the fifo and return to idle
 *
 * Deliberately has no caller in the autonomous path, and should not gain one: a captured frame
 * has to survive between contact passes for a downlink to be resumable (REQ-PAY-004), so nothing
 * on the vehicle may quietly bin it. Starting a new capture already flushes the fifo itself.
 * This exists for a ground-commanded discard - the ground deciding an image is not worth the
 * pass - which wants a command in the catalog before it means anything.
 */
void ov2640_discard(void);

/**
 * @brief  raw arduchip register read - bring-up and hil diagnostics
 * @param  reg  arduchip register address
 * @return the byte it returned
 *
 * the driver's own state machine never needs this; it exists so a failure can be described in
 * terms of what the chip actually reported rather than inferred from a state enum
 */
uint8_t ov2640_chip_reg(uint8_t reg);

#ifdef __cplusplus
}
#endif

#endif  // OV2640_H
