/**
 * @file   platform.hpp
 * @brief  platform-abstraction layer - the fsw's active i/o with the outside (time base + link).
 *         inbound data (commands, sensor samples) arrives via Inputs, assembled by the caller.
 */

#ifndef FSW_PLATFORM_HPP
#define FSW_PLATFORM_HPP

#include <cstdint>

namespace fsw::platform {

/**
 * @brief  hand a framed telemetry message to the link
 * @param  frame the frame to hand over
 * @param  len   the length of the frame
 */
void send_telemetry(const uint8_t* frame, uint32_t len);

}  // namespace fsw::platform

#endif  // FSW_PLATFORM_HPP
