/**
 * @file   ArduCAM.h
 * @brief  NOT upstream code - a minimal stand-in for arducam's header
 *
 * ov2640_regs.h is vendored byte-exact from arducam's arduino library so it can be re-fetched and
 * diffed against upstream. it opens with `#include "ArduCAM.h"` and tags every table `PROGMEM`,
 * both of which only exist inside the arduino/avr world. rather than edit the vendored file - which
 * would destroy the property that makes vendoring worth doing - this supplies the two things it
 * needs and nothing else.
 *
 * upstream: https://github.com/ArduCAM/Arduino (ArduCAM/ov2640_regs.h)
 */

#ifndef ARDUCAM_SHIM_H
#define ARDUCAM_SHIM_H

#include <stdint.h>

// avr keeps constant tables in flash and needs them marked; a cortex-m addresses flash directly,
// so this is a no-op here
#define PROGMEM

// one sccb register write. must stay layout-compatible with ov2640_reg_t in
// obc/Inc/devices/ov2640_regs.h - the adapter casts between them and static_asserts the size
struct sensor_reg {
    uint8_t reg;
    uint8_t val;
};

#endif  // ARDUCAM_SHIM_H
