/**
 * @file   platform.hpp
 * @brief  platform-abstraction layer - the only door from flight logic to the outside
 */

#ifndef FSW_PLATFORM_HPP
#define FSW_PLATFORM_HPP

#include <cstdint>

namespace fsw::platform {

// milliseconds since boot (steady_clock on the host, SysTick on the target)
uint32_t now_ms();

/**
 * @brief  hand a framed telemetry message to the link
 * @param  frame the frame to hand over
 * @param  len   the length of the frame
 */
void send_telemetry(const uint8_t* frame, uint32_t len);

// sensor reads (read_imu, read_bus_voltage, ...) join here as the sensor phase lands

}  // namespace fsw::platform

#endif  // FSW_PLATFORM_HPP
