/**
 * @file   ov2640_regs.h
 * @brief  ov2640 sccb configuration tables - vendor data, not hand-written
 *
 * the ov2640's jpeg bring-up is a few hundred register writes whose values the datasheet does not
 * explain; every driver in the wild uses omnivision's tables by way of arducam's library. they are
 * vendored rather than retyped, for the same reason etl and cmsis are: a transcription error here
 * produces a sensor that configures cleanly and returns garbage, with nothing to point at.
 *
 * the tables themselves live in vendor/arducam/, byte-exact from upstream. this header is the
 * driver-facing view of them, and ov2640_regs.c is the adapter between the two
 */

#ifndef OBC_OV2640_REGS_H
#define OBC_OV2640_REGS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// one sccb write. {0xFF, 0xFF} terminates a table
typedef struct {
    uint8_t reg;
    uint8_t val;
} ov2640_reg_t;

// false if the vendored tables are missing - init reports it rather than pretending
extern const bool ov2640_tables_vendored;

extern const ov2640_reg_t* const ov2640_jpeg_init;    // sensor + dsp setup, jpeg pipeline on
extern const ov2640_reg_t* const ov2640_yuv422;       // yuv422 before the jpeg encoder is enabled
extern const ov2640_reg_t* const ov2640_jpeg_enable;  // switch the output format to jpeg
extern const ov2640_reg_t* const ov2640_320x240;      // output size - one table per resolution
extern const ov2640_reg_t* const ov2640_800x600;
extern const ov2640_reg_t* const ov2640_1600x1200;

#ifdef __cplusplus
}
#endif

#endif  // OBC_OV2640_REGS_H
