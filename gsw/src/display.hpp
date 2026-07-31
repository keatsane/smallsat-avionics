/**
 * @file   display.hpp
 * @brief  the ssd1306 readout - what the box says with no pc attached
 *
 * This is the one part of the ground station that decodes anything, and it uses the satellite's
 * own state.hpp catalogs to do it. A mode or fault renamed on the vehicle renames here on the
 * next build rather than quietly showing the wrong word.
 */

#ifndef GSW_DISPLAY_HPP
#define GSW_DISPLAY_HPP

#include <stdbool.h>
#include <stdint.h>

#include "protocol/msg.hpp"

/** @brief bring up the panel; false if it did not ack on i2c */
bool display_begin();

/** @brief take the newest decoded heartbeat */
void display_heartbeat(const fsw::heartbeat_t& hb);

/** @brief redraw - call from the loop, it rate-limits itself */
void display_service(uint32_t lora_packets, uint32_t nrf_packets, bool lora_up, bool nrf_up);

#endif  // GSW_DISPLAY_HPP
