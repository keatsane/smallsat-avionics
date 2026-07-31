/**
 * @file   main.cpp
 * @brief  ground station - hears the vehicle on two radios and pumps it out usb serial
 *
 * The whole job is to be a frame pipe. A packet arrives over the air, whole frames are recovered
 * from it and written to the usb port, and `tools/uart_monitor.py` decodes them with the same
 * FrameDecoder it already uses on the st-link cable - because the frame format does not change per
 * transport, which is the point REQ-TLM-004 exists to make.
 *
 * Frames rather than bytes, because the two radios do not deliver a clean byte stream: nRF24
 * packets carry padding and LoRa packets arrive between them. Decoding per radio and re-encoding
 * on the way out gives the pc exactly what the satellite emitted, from both links, on one port.
 *
 * The display reads the heartbeats going past, using the satellite's own catalogs (display.cpp).
 *
 * Uplink works the same way in reverse: whatever the pc types arrives here as encoded command
 * frames on usb, and they go out over lora unexamined. `uart_monitor.py` builds them exactly as
 * it does for the wired console, so commanding over the air needed no ground-tool change at all.
 */

#include <Arduino.h>

#include "display.hpp"
#include "protocol/frame.hpp"
#include "protocol/msg.hpp"
#include "radios.hpp"

namespace {

constexpr uint8_t kPinLed = 13;

// the led says which of three things is true, because a board that is fine and a board that is
// dead look identical when the only signal is "off". fast blink is init failure; a brief flash
// every 2 s is alive but hearing nothing; a longer flash is a frame that passed crc
constexpr uint32_t kAliveBlinkMs = 2000;
constexpr uint32_t kAliveFlashMs = 20;
constexpr uint32_t kFrameFlashMs = 60;

// how long after the last payload packet the screen keeps its hands off the bus
constexpr uint32_t kDownlinkQuietMs = 500;

uint32_t led_off_at = 0;
uint32_t next_alive_blink = 0;

bool lora_up = false;
bool nrf_up = false;

// one parser PER RADIO. sharing them was a real bug: a heartbeat arriving over lora between two
// nrf24 payload packets spliced itself into the middle of an image frame, and the interleaved
// bytes went out usb in that order too, so the pc saw the same wreckage
struct link_t {
    fsw::frame_parser_t parser;
    uint32_t frames;  // whole crc-checked frames recovered on this link since boot
};

link_t lora_link{};
link_t nrf_link{};

// how often the ground station says how it is doing. it used to say so once, at boot, and a
// receiver that failed to initialise then was indistinguishable from a working one hearing
// nothing - which is exactly what hid a dead payload radio while the vehicle transmitted a whole
// image into it. once a second, forever, so it is true whenever somebody opens a terminal
constexpr uint32_t kGroundStatusMs = 1000;
uint32_t next_status_ms = 0;

fsw::frame_parser_t up_parser;  // uplink, from usb - re-encoded on the way out, same as the rx side

// when the payload link last delivered anything, so the screen can stay out of its way
uint32_t last_nrf_ms = 0;

void led_flash(uint32_t ms) {
    digitalWrite(kPinLed, HIGH);
    led_off_at = millis() + ms;
}

void led_service() {
    const uint32_t now = millis();
    if (led_off_at != 0 && static_cast<int32_t>(now - led_off_at) >= 0) {
        digitalWrite(kPinLed, LOW);
        led_off_at = 0;
    }
    if (led_off_at == 0 && static_cast<int32_t>(now - next_alive_blink) >= 0) {
        next_alive_blink = now + kAliveBlinkMs;
        led_flash(kAliveFlashMs);
    }
}

// decode one link's bytes, and re-encode every whole frame onto usb.
//
// re-encoding rather than forwarding the received bytes is the point. the obvious version keeps a
// shadow copy of what arrived and writes that when the parser says a frame ended - and it does not
// work, because the bytes on the air are not only frame bytes. the nrf24 pads every packet to 32,
// a payload frame is 70, so each frame arrives as three packets carrying 70 frame bytes and 26
// zeros. those zeros land in the shadow buffer, the next frame overruns it, and what goes to the
// pc is padding followed by a truncated frame. every image frame failed crc at the pc for this
// reason and nothing was ever reassembled.
//
// the parser already found the frame boundaries and checked the crc. frame_encode is deterministic,
// so re-encoding gives back exactly the bytes the satellite emitted, with no padding and no way for
// two radios to interleave mid-frame
void on_link_rx(link_t& link, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        auto f = fsw::frame_decode(&link.parser, data[i]);
        if (!f) {
            continue;
        }

        uint8_t out[fsw::kFrameMaxSize];
        const size_t n = fsw::frame_encode(f->msg_id, f->payload, f->len, out);
        Serial.write(out, n);
        link.frames++;

        led_flash(kFrameFlashMs);
        next_alive_blink = millis() + kAliveBlinkMs;  // do not stack an alive blink on top

        if (f->msg_id == static_cast<uint8_t>(fsw::MsgId::Heartbeat) &&
            f->len == sizeof(fsw::heartbeat_t)) {
            fsw::heartbeat_t hb{};
            memcpy(&hb, f->payload, sizeof(hb));
            display_heartbeat(hb);
        } else if (f->msg_id == static_cast<uint8_t>(fsw::MsgId::DownlinkStatus) &&
                   f->len == sizeof(fsw::downlink_status_t)) {
            fsw::downlink_status_t d{};
            memcpy(&d, f->payload, sizeof(d));
            display_downlink(d);
        }
    }
}

void on_lora_rx(const uint8_t* data, size_t len) { on_link_rx(lora_link, data, len); }

void on_nrf_rx(const uint8_t* data, size_t len) {
    last_nrf_ms = millis();
    on_link_rx(nrf_link, data, len);
}

// take whatever the pc sent and put whole frames on the air. anything that is not a frame is
// dropped rather than transmitted - the decoder is the judge of what counts
void service_uplink() {
    while (Serial.available() > 0) {
        const int b = Serial.read();
        if (b < 0) {
            return;
        }

        auto f = fsw::frame_decode(&up_parser, static_cast<uint8_t>(b));
        if (!f) {
            continue;
        }

        uint8_t out[fsw::kFrameMaxSize];
        const size_t n = fsw::frame_encode(f->msg_id, f->payload, f->len, out);
        lora_send(out, n);
    }
}

// the ground station's own health, as a frame like everything else - so the console decodes it
// with the decoder it already has rather than parsing a line of text
void service_ground_status() {
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - next_status_ms) < 0) {
        return;
    }
    next_status_ms = now + kGroundStatusMs;

    fsw::ground_status_t g{};
    g.t_ms = now;
    g.lora_frames = lora_link.frames;
    g.nrf24_frames = nrf_link.frames;
    g.nrf24_packets = nrf24_packets();
    g.nrf24_busy_pct = nrf_up ? nrf24_channel_busy_pct() : 0U;
    g.flags = static_cast<uint8_t>((lora_up ? fsw::kGroundFlagLoraUp : 0U) |
                                   (nrf_up ? fsw::kGroundFlagNrf24Up : 0U) |
                                   (display_have_status_panel() ? fsw::kGroundFlagPanel1 : 0U) |
                                   (display_have_payload_panel() ? fsw::kGroundFlagPanel2 : 0U));

    uint8_t out[fsw::kFrameMaxSize];
    const size_t n = fsw::frame_encode(static_cast<uint8_t>(fsw::MsgId::GroundStatus),
                                       reinterpret_cast<const uint8_t*>(&g), sizeof(g), out);
    Serial.write(out, n);
}

}  // namespace

void setup() {
    pinMode(kPinLed, OUTPUT);
    digitalWrite(kPinLed, LOW);

    Serial.begin(115200);
    // deliberately not waiting for a host - a ground station that hangs until someone opens a
    // terminal is a ground station that looks dead when it is fine

    // the display first, so a radio that fails to come up has somewhere to say so
    display_begin();

    lora_up = lora_begin();
    nrf_up = nrf24_begin();

    if (!lora_up || !nrf_up) {
        // the only text this ever prints, and it cannot be mistaken for telemetry: a frame starts
        // AA 55 and the decoder resyncs past anything else
        Serial.print("GSW: init failed -");
        if (!lora_up) {
            Serial.print(" lora");
        }
        if (!nrf_up) {
            Serial.print(" nrf24");
        }
        Serial.println(" - check cs wiring and the 3v3 rail");
    }

    // a radio that never came up is a hard stop only if BOTH are down - one working link is still
    // a ground station, and saying which one failed is more useful than refusing to run
    if (!lora_up && !nrf_up) {
        while (true) {
            digitalWrite(kPinLed, HIGH);
            delay(100);
            digitalWrite(kPinLed, LOW);
            delay(100);
            display_service(display_counts_t{0, 0, 0, 0, false, false});
        }
    }
}

void loop() {
    if (lora_up) {
        lora_poll(on_lora_rx);
    }
    if (nrf_up) {
        nrf24_poll(on_nrf_rx);
    }
    if (lora_up) {
        service_uplink();
    }
    led_service();

    // deliberately outside the downlink-quiet gate below: the whole point of this frame is to be
    // heard during a downlink that is going wrong, and it is 23 bytes over usb rather than 25 ms
    // on the i2c bus
    service_ground_status();

    // the screen stays out of the payload link's way for as long as the downlink lasts. a full
    // ssd1306 redraw pushes 1 KB over i2c and blocks ~25 ms, and at ~470 packets a second that is
    // a dozen packets arriving into a fifo three deep.
    //
    // checking "is a packet waiting right now" was not enough - the fifo is momentarily empty
    // between bursts, the draw starts anyway, and the next 25 ms of packets are lost. so the test
    // is whether the link has been active recently at all
    if (nrf_up && (nrf24_pending() || (millis() - last_nrf_ms) < kDownlinkQuietMs)) {
        return;
    }

    const display_counts_t counts{lora_packets(),  nrf24_packets(), lora_link.frames,
                                  nrf_link.frames, lora_up,         nrf_up};
    display_service(counts);
}
