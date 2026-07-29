/**
 * @file   ov2640_regs.cpp
 * @brief  adapter between arducam's vendored sccb tables and the driver's view of them
 *
 * the only file that knows upstream's naming. two deliberate choices here:
 *
 * it is c++ rather than c because the vendored file is - upstream declares some tables as
 * `const struct sensor_reg X[] { ... }`, brace init with no `=`, which is legal c++ and not legal
 * c. compiling this one translation unit as c++ costs nothing and keeps the vendored file
 * byte-exact, which is the entire point of vendoring it.
 *
 * and it pulls the tables in by relative path rather than an include directory because cubeide
 * regenerates its makefiles from the project tree - a hand-added -I would silently disappear on
 * the next refresh and take the build with it
 */

#include "devices/ov2640_regs.h"

#include "../../../vendor/arducam/ov2640_regs.h"  // defines the OV2640_* tables

// upstream's struct and ours are separate types holding the same two bytes; the casts below are
// only honest while that stays true
static_assert(sizeof(struct sensor_reg) == sizeof(ov2640_reg_t),
              "arducam's sensor_reg no longer matches ov2640_reg_t");

const bool ov2640_tables_vendored = true;

// upstream's jpeg bring-up order, from arducam's own examples: base init, yuv422, jpeg, then size
const ov2640_reg_t* const ov2640_jpeg_init =
    reinterpret_cast<const ov2640_reg_t*>(OV2640_JPEG_INIT);
const ov2640_reg_t* const ov2640_yuv422 = reinterpret_cast<const ov2640_reg_t*>(OV2640_YUV422);
const ov2640_reg_t* const ov2640_jpeg_enable = reinterpret_cast<const ov2640_reg_t*>(OV2640_JPEG);
const ov2640_reg_t* const ov2640_320x240 =
    reinterpret_cast<const ov2640_reg_t*>(OV2640_320x240_JPEG);
