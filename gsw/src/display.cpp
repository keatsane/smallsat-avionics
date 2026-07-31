/**
 * @file   display.cpp
 * @brief  see display.hpp - the only part of the ground station that reads a message
 */

#include "display.hpp"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "protocol/state.hpp"

namespace {

constexpr uint8_t kWidth = 128;
constexpr uint8_t kHeight = 64;
constexpr uint8_t kI2cAddr = 0x3C;  // some modules ship as 0x3D - check with an i2c scan if blank

// redraw rate. the panel is slow over i2c and nothing on it changes faster than 1 Hz, so pushing
// frames faster than this only steals time from draining the radios
constexpr uint32_t kRedrawMs = 250;

// how long without a heartbeat before the link is called lost. three beacons' worth - one missed
// packet is ordinary at any range, three in a row is a link
constexpr uint32_t kLinkLostMs = 3500;

Adafruit_SSD1306 oled(kWidth, kHeight, &Wire, -1);

bool ready = false;
bool ever_heard = false;
uint32_t last_hb_ms = 0;
uint32_t next_draw_ms = 0;
fsw::heartbeat_t hb{};

uint8_t count_bits(uint32_t v) {
    uint8_t n = 0;
    while (v != 0U) {
        n = static_cast<uint8_t>(n + (v & 1U));
        v >>= 1;
    }
    return n;
}

}  // namespace

bool display_begin() {
    Wire.begin();
    ready = oled.begin(SSD1306_SWITCHCAPVCC, kI2cAddr);
    if (!ready) {
        return false;
    }
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println("SSAV-1 ground");
    oled.println("waiting for beacon");
    oled.display();
    return true;
}

void display_heartbeat(const fsw::heartbeat_t& in) {
    hb = in;
    ever_heard = true;
    last_hb_ms = millis();
}

void display_service(uint32_t lora_packets, uint32_t nrf_packets, bool lora_up, bool nrf_up) {
    if (!ready) {
        return;
    }
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - next_draw_ms) < 0) {
        return;
    }
    next_draw_ms = now + kRedrawMs;

    const uint32_t age = now - last_hb_ms;
    const bool linked = ever_heard && age < kLinkLostMs;

    oled.clearDisplay();
    oled.setCursor(0, 0);

    // line 1, double height - the mode is the thing you read from across the room
    oled.setTextSize(2);
    oled.println(ever_heard ? fsw::mode_name(hb.mode) : "NO SIG");
    oled.setTextSize(1);

    if (!ever_heard) {
        oled.println();
        oled.println("listening...");
        oled.print("lora ");
        oled.println(lora_up ? "up" : "DOWN");
        oled.print("nrf24 ");
        oled.println(nrf_up ? "up" : "DOWN");
        oled.display();
        return;
    }

    // the header carries the distinction the satellite's own fault bead makes, because a single
    // character in front of a name is far too easy to miss: a latched fault whose response is
    // suppressed is not an alarm, and a screen that showed "FAULT 2" for a bench build with two
    // declared inhibits would be crying wolf every session
    const uint8_t n = count_bits(hb.faults);
    const uint8_t acting = count_bits(hb.faults & ~hb.inhibited);

    if (n == 0U) {
        oled.println("no faults");
    } else if (acting == 0U) {
        oled.print("INHIB ");  // all latched, none acted on - the satellite's blue rung
        oled.print(n);
        oled.println(n == 1 ? " fault" : " faults");
    } else {
        oled.print("FAULT ");
        oled.print(acting);
        oled.print(" of ");
        oled.println(n);
    }

    // names, acting ones first - if only two fit, they should be the two that matter
    uint8_t shown = 0;
    for (uint8_t pass = 0; pass < 2 && shown < 2U; pass++) {
        const bool want_acting = (pass == 0);
        for (uint8_t i = 0U; i < fsw::kFaultCount && shown < 2U; i++) {
            if ((hb.faults & (1UL << i)) == 0U) {
                continue;
            }
            const bool inhibited = (hb.inhibited & (1UL << i)) != 0U;
            if (inhibited == want_acting) {
                continue;
            }
            oled.print(inhibited ? "i " : "! ");
            oled.println(fsw::fault_name(i));
            shown++;
        }
    }
    if (n > shown) {
        oled.print("  +");
        oled.print(n - shown);
        oled.println(" more");
    }

    oled.print("seq ");
    oled.print(hb.seq);
    oled.print("  up ");
    oled.print(hb.uptime_ms / 1000UL);
    oled.println("s");

    oled.print(linked ? "LINK " : "LOST ");
    oled.print(lora_packets);
    oled.print("L ");
    oled.print(nrf_packets);
    oled.println("N");

    oled.display();
}
