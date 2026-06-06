/**
 * @file   platform.hpp
 * @brief  platform-abstraction layer - the only door from flight logic to the outside
 *
 * the flight software never touches registers or the link directly. it calls these
 * free functions, and the build links a backend: the host backend (platform/host) for
 * SIL and unit tests, the STM32 firmware drivers on the target. same source, swapped
 * backend - so the decisions are tested on the PC long before any wires are attached.
 */

#ifndef FSW_PLATFORM_HPP
#define FSW_PLATFORM_HPP

#include <cstdint>

namespace fsw::platform {

// milliseconds since boot (steady_clock on the host, SysTick on the target)
uint32_t now_ms();

// hand a framed telemetry message to the link (stdout on the host, USART2 on the target)
void send_telemetry(const uint8_t* frame, uint32_t len);

// sensor reads (read_imu, read_bus_voltage, ...) join here as the sensor phase lands

}  // namespace fsw::platform

#endif  // FSW_PLATFORM_HPP
