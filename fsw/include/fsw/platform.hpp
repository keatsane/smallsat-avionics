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

/**
 * @brief  command a reaction-wheel torque
 * @param  torque_nm  newton metres, signed - the sign is the spin direction
 *
 * an action the fsw performs, like send_telemetry, not an input it reads. the esc closes the
 * current loop itself and clamps to its own limit, so this is a request, not a demand.
 *
 * newton metres, not the q-axis millivolts that actually go on the wire: converting torque to a
 * voltage needs a specific motor's torque constant and winding resistance, and a control law that
 * carries those stops being portable the day the motor changes (REQ-PAL-001). the backend owns
 * the conversion, the same way it owns turning gyro counts into a rate on the way in
 */
void set_wheel_torque_nm(float torque_nm);

/**
 * @brief  ask the payload camera to take a frame
 * @param  resolution  which output size, as a catalog id (ImageResolution in state.hpp)
 *
 * fire-and-forget, like set_wheel_torque: the camera's fifo holds the frame and the outcome comes
 * back as camera_data_t on a later cycle, so the fsw never blocks on the sensor.
 *
 * the size crosses as a catalog id rather than as pixels, for the same reason torque crosses in
 * newton-metres rather than volts: which register table produces 800x600 on this sensor is the
 * backend's business, and the flight software would stop being portable the day the camera changed
 */
void capture_image(uint8_t resolution);

/**
 * @brief  say whether the payload buffer should be draining to the ground
 * @param  active  true while the vehicle is in DOWNLINK
 *
 * the image never enters flight-software memory - the payload's own buffer holds it and the
 * platform reads straight out of it (REQ-PAY-003). the fsw decides only *whether* to downlink;
 * how fast is a property of the link, which is knowledge that belongs on this side of the
 * boundary. call every cycle, so leaving DOWNLINK stops the stream
 */
void set_payload_downlink(bool active);

}  // namespace fsw::platform

#endif  // FSW_PLATFORM_HPP
