/**
 * @file   display.cpp
 * @brief  see display.hpp - the only part of the ground station that reads a message
 */

#include "display.hpp"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <math.h>

#include "protocol/state.hpp"

namespace {

constexpr uint8_t kWidth = 128;
constexpr uint8_t kHeight = 64;
// the SSD1306's two selectable addresses. the panel picks between them with one jumper or one
// resistor on its back, which is the whole reason two of these can share four wires
constexpr uint8_t kStatusAddr = 0x3C;
constexpr uint8_t kPayloadAddr = 0x3D;

// redraw rate. the panel is slow over i2c and nothing on it changes faster than 1 Hz, so pushing
// frames faster than this only steals time from draining the radios
constexpr uint32_t kRedrawMs = 250;

// how long the boot splash stays up. long enough to read which panel is which, short enough that
// nobody wonders whether the box has hung
constexpr uint32_t kSplashMs = 2500;

// redraw rate while an image is coming down. a panel costs ~25 ms of blocking i2c, which at the
// burst rate is about ten lost packets - so once a second, and only the payload panel. that is
// 2.5% of a pass spent drawing the thing that shows the pass, which is a trade worth making now
// that the image goes round three times
constexpr uint32_t kBusyRedrawMs = 1000;

// how long without a heartbeat before the link is called lost. three beacons' worth - one missed
// packet is ordinary at any range, three in a row is a link
constexpr uint32_t kLinkLostMs = 3500;

Adafruit_SSD1306 oled(kWidth, kHeight, &Wire, -1);
Adafruit_SSD1306 oled2(kWidth, kHeight, &Wire, -1);

bool ready = false;
bool ready2 = false;

// what the bus said, kept apart from whether the panel then initialised - "nothing is at 0x3D" and
// "something is at 0x3D and would not start" are different problems with different fixes
bool found_status = false;
bool found_payload = false;
bool ever_heard = false;
uint32_t last_hb_ms = 0;
uint32_t next_draw_ms = 0;
fsw::heartbeat_t hb{};

// the newest downlink report, and when it arrived. the second panel says "idle" rather than
// holding a stale bar forever, because a bar frozen at 60% reads like a stall rather than a
// finished pass
fsw::downlink_status_t dl{};
bool ever_downlinked = false;
uint32_t last_dl_ms = 0;
constexpr uint32_t kDownlinkStaleMs = 5000;

fsw::attitude_status_t att{};
bool ever_attitude = false;

uint8_t count_bits(uint32_t v) {
    uint8_t n = 0;
    while (v != 0U) {
        n = static_cast<uint8_t>(n + (v & 1U));
        v >>= 1;
    }
    return n;
}

// does anything acknowledge its address on the bus?
//
// done here rather than left to Adafruit_SSD1306::begin, which reported a panel on 0x3D that was
// not there: its detection is one beginTransmission/endTransmission pair, and endTransmission does
// not reliably report a missing device on every core. the symptom was two modules both sitting on
// 0x3C, both drawing the status screen, while the firmware believed it had two panels and wrote
// half its output into nothing.
//
// a write of one byte is a stronger test than an empty transmission - a device that acks its
// address but not a data byte is not a device this can drive
bool i2c_present(uint8_t addr) {
    Wire.beginTransmission(addr);
    Wire.write(0x00);  // the SSD1306's command-stream control byte, harmless on its own
    return Wire.endTransmission() == 0;
}

}  // namespace

bool display_begin() {
    Wire.begin();

    found_status = i2c_present(kStatusAddr);
    found_payload = i2c_present(kPayloadAddr);

    ready = found_status && oled.begin(SSD1306_SWITCHCAPVCC, kStatusAddr);
    if (ready) {
        oled.clearDisplay();
        oled.setTextColor(SSD1306_WHITE);
        oled.setTextSize(2);
        oled.setCursor(0, 0);
        // each panel names itself and its address at boot, and holds it long enough to read. two
        // modules left on the same address both answer, both get written, and the one drawn last
        // wins on both - which looks like a duplicated screen rather than like a wiring mistake.
        // naming them is the difference between "why are these the same" and "both say 0x3C"
        oled.println("PANEL 1");
        oled.setTextSize(1);
        oled.println("status  addr 0x3C");
        oled.println();
        oled.println("SSAV-1 ground");
        oled.println("waiting for beacon");
        oled.display();
    }

    // the second panel is allowed not to exist, and is only brought up if the bus says it is
    // there. two modules on one address is the likely mistake, and it has to read as "one panel"
    // rather than as two - otherwise half the output goes to an address nothing is listening on
    ready2 = found_payload && oled2.begin(SSD1306_SWITCHCAPVCC, kPayloadAddr);
    if (ready2) {
        oled2.clearDisplay();
        oled2.setTextColor(SSD1306_WHITE);
        oled2.setTextSize(2);
        oled2.setCursor(0, 0);
        oled2.println("PANEL 2");
        oled2.setTextSize(1);
        oled2.println("payload  addr 0x3D");
        oled2.println();
        oled2.println("idle");
        oled2.display();
    }

    // hold both splashes long enough to be read before the first redraw paints over them. this is
    // the only blocking wait in the ground station and it happens once, before any radio is live
    if (ready || ready2) {
        delay(kSplashMs);
    }

    return ready;
}

bool display_have_status_panel() { return found_status; }

bool display_have_payload_panel() { return found_payload; }

void display_attitude(const fsw::attitude_status_t& in) {
    att = in;
    ever_attitude = true;
}

void display_downlink(const fsw::downlink_status_t& in) {
    dl = in;
    ever_downlinked = true;
    last_dl_ms = millis();
}

void display_heartbeat(const fsw::heartbeat_t& in) {
    hb = in;
    ever_heard = true;
    last_hb_ms = millis();
}

namespace {

// the attitude dial: where the vehicle is pointing, and where it was told to point.
//
// a filled tick for the heading and a hollow one for the target, on a circle whose zero is
// wherever POINTING was entered - there is no north on this vehicle, so drawing a compass would be
// claiming a reference it does not have. the two ticks converging is the whole of what a pointing
// run looks like
void draw_dial(int16_t cx, int16_t cy, int16_t r) {
    oled2.drawCircle(cx, cy, r, SSD1306_WHITE);
    oled2.drawPixel(cx, cy - r, SSD1306_WHITE);  // the zero mark, at the top

    if (!ever_attitude) {
        return;
    }

    // screen y grows downward and zero is up, so the usual sin/cos are swapped and negated. done
    // once here rather than in each caller, where getting it wrong makes the dial turn backwards
    const float hdg = att.heading_mrad / 1000.0F;
    const float tgt = att.target_mrad / 1000.0F;

    const int16_t hx = cx + static_cast<int16_t>(sinf(hdg) * (r - 2));
    const int16_t hy = cy - static_cast<int16_t>(cosf(hdg) * (r - 2));
    oled2.drawLine(cx, cy, hx, hy, SSD1306_WHITE);

    const int16_t tx = cx + static_cast<int16_t>(sinf(tgt) * r);
    const int16_t ty = cy - static_cast<int16_t>(cosf(tgt) * r);
    oled2.drawCircle(tx, ty, 2, SSD1306_WHITE);
}

// the payload panel: how the current downlink is going, and what the payload radio has actually
// heard. the counters are the useful half - a progress bar climbing while nrf frames stays at zero
// is the vehicle transmitting into a receiver that is not there, which is a thing worth seeing
void draw_payload_panel(uint32_t nrf_packets, uint32_t nrf_frames, bool nrf_up) {
    oled2.clearDisplay();

    // fixed regions, none touching: header top-left, dial top-right (x >= 98, y <= 28), the
    // progress bar as a full-width band in the middle, counters along the bottom. the first
    // layout let the text rows run under the dial and the overlap was legible from across the
    // room - in the bad way
    oled2.setCursor(0, 0);
    oled2.setTextSize(2);
    oled2.println("PAYLOAD");
    oled2.setTextSize(1);

    draw_dial(kWidth - 16, 15, 13);

    const bool active = ever_downlinked && (millis() - last_dl_ms) < kDownlinkStaleMs;

    oled2.setCursor(0, 20);
    if (!active) {
        oled2.print(ever_downlinked ? "last pass done" : "idle");
    } else {
        oled2.print("img ");
        oled2.print(dl.image_id);
        oled2.print(" ");
        oled2.print(dl.chunk);
        oled2.print("/");
        oled2.print(dl.chunks);
    }

    // a drawn bar rather than characters - the one thing here readable at a glance
    const int16_t bar_y = 32;
    const int16_t bar_w = kWidth - 4;
    oled2.drawRect(2, bar_y, bar_w, 9, SSD1306_WHITE);
    if (active && dl.chunks > 0U) {
        const int32_t fill = (static_cast<int32_t>(dl.chunk) * (bar_w - 4)) / dl.chunks;
        oled2.fillRect(4, bar_y + 2, static_cast<int16_t>(fill), 5, SSD1306_WHITE);
    }

    oled2.setCursor(0, 45);
    oled2.print("nrf ");
    oled2.print(nrf_up ? "up" : "DOWN");
    oled2.print("  pkt ");
    oled2.print(nrf_packets);
    oled2.setCursor(0, 55);
    oled2.print("fr ");
    oled2.print(nrf_frames);
    if (ever_downlinked) {
        oled2.print("  drop ");
        oled2.print(dl.radio_dropped);
    }

    oled2.display();
}

void draw_status_panel(const display_counts_t& c) {
    const uint32_t now = millis();
    const uint32_t age = now - last_hb_ms;
    const bool linked = ever_heard && age < kLinkLostMs;
    const uint32_t lora_packets = c.lora_packets;
    const uint32_t nrf_packets = c.nrf_packets;
    const bool lora_up = c.lora_up;
    const bool nrf_up = c.nrf_up;

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

}  // namespace

void display_service(const display_counts_t& c) {
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - next_draw_ms) < 0) {
        return;
    }
    next_draw_ms = now + (c.downlinking ? kBusyRedrawMs : kRedrawMs);

    // during a pass the status panel is the one to sacrifice: mode and faults are not changing,
    // and the progress bar is the only thing anybody is watching
    if (ready && !c.downlinking) {
        draw_status_panel(c);
    }
    if (ready2) {
        draw_payload_panel(c.nrf_packets, c.nrf_frames, c.nrf_up);
    }
}
