/**
 * @file   display.hpp
 * @brief  the ssd1306 readout - what the box says with no pc attached
 *
 * This is the one part of the ground station that decodes anything, and it uses the satellite's
 * own state.hpp catalogs to do it. A mode or fault renamed on the vehicle renames here on the
 * next build rather than quietly showing the wrong word.
 *
 * Two panels are supported, on the SSD1306's two selectable i2c addresses. The status panel
 * (0x3C) carries mode, faults and link. The payload panel (0x3D) carries the downlink - progress
 * and radio counters - and is entirely optional: a box with one screen is a box with one screen,
 * and nothing here changes behaviour when the second never answers.
 */

#ifndef GSW_DISPLAY_HPP
#define GSW_DISPLAY_HPP

#include <stdbool.h>
#include <stdint.h>

#include "protocol/msg.hpp"

// what the panels are told about the links, gathered in one place so adding a counter does not
// mean touching every call site
struct display_counts_t {
    uint32_t lora_packets;  // raw packets off each radio
    uint32_t nrf_packets;
    uint32_t lora_frames;  // whole crc-checked frames recovered from them
    uint32_t nrf_frames;
    bool lora_up;
    bool nrf_up;
};

/** @brief bring up whichever panels answer; false if not even the status one did */
bool display_begin();

/** @brief whether the status panel answered on 0x3C */
bool display_have_status_panel();

/** @brief whether the optional second panel answered on 0x3D */
bool display_have_payload_panel();

/** @brief take the newest decoded heartbeat */
void display_heartbeat(const fsw::heartbeat_t& hb);

/** @brief take the newest decoded downlink progress report */
void display_downlink(const fsw::downlink_status_t& d);

/** @brief redraw - call from the loop, it rate-limits itself */
void display_service(const display_counts_t& c);

#endif  // GSW_DISPLAY_HPP
