/**
 * @file   main.cpp
 * @brief  ground station - hears the vehicle on two radios and pumps it out usb serial
 *
 * The whole job is to be a byte pipe. A packet arrives over the air, its bytes go out the usb
 * port, and `tools/uart_monitor.py` decodes them with the same FrameDecoder it already uses on
 * the st-link cable - because the frame format does not change per transport, which is the point
 * REQ-TLM-004 exists to make. Neither radio parses anything.
 *
 * Both streams go out the one port, deliberately. They are the same frame format, the decoder
 * resyncs on AA 55 and checks a crc, and the ground tooling already reassembles payload chunks
 * from a single byte stream - so LoRa beacons and nRF24 image chunks interleaving costs nothing
 * and needs no demultiplexing at either end.
 *
 * The display is the one thing here that decodes a message, and it uses the satellite's own
 * catalogs to do it (see display.cpp).
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

uint32_t led_off_at = 0;
uint32_t next_alive_blink = 0;

bool lora_up = false;
bool nrf_up = false;

fsw::frame_parser_t parser;     // downlink, from the radios
fsw::frame_parser_t up_parser;  // uplink, from usb

// bytes typed at the pc, held until they form a whole frame. commands are 10 bytes, so this only
// ever holds one - but transmitting a half frame would put a packet on the air that the vehicle
// throws away, and the ground would have no idea why
uint8_t up_buf[fsw::kFrameMaxSize];
size_t up_len = 0;

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

// every received byte goes here, from either radio. out the usb port unexamined, and through the
// satellite's own decoder purely so the box can light an led and draw a screen
void on_rx(const uint8_t* data, size_t len) {
    Serial.write(data, len);

    for (size_t i = 0; i < len; i++) {
        auto f = fsw::frame_decode(&parser, data[i]);
        if (!f) {
            continue;
        }
        led_flash(kFrameFlashMs);
        next_alive_blink = millis() + kAliveBlinkMs;  // do not stack an alive blink on top

        if (f->msg_id == static_cast<uint8_t>(fsw::MsgId::Heartbeat) &&
            f->len == sizeof(fsw::heartbeat_t)) {
            fsw::heartbeat_t hb{};
            memcpy(&hb, f->payload, sizeof(hb));
            display_heartbeat(hb);
        }
    }
}

// take whatever the pc sent and put whole frames on the air. anything that is not a frame is
// dropped rather than transmitted - the decoder is the judge of what counts
void service_uplink() {
    while (Serial.available() > 0) {
        const int b = Serial.read();
        if (b < 0) {
            return;
        }

        if (up_len < sizeof(up_buf)) {
            up_buf[up_len++] = static_cast<uint8_t>(b);
        } else {
            up_len = 0;  // never a frame - resync rather than transmit junk
        }

        if (fsw::frame_decode(&up_parser, static_cast<uint8_t>(b))) {
            lora_send(up_buf, up_len);
            up_len = 0;
        }
    }
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
            display_service(0, 0, false, false);
        }
    }
}

void loop() {
    if (lora_up) {
        lora_poll(on_rx);
    }
    if (nrf_up) {
        nrf24_poll(on_rx);
    }
    if (lora_up) {
        service_uplink();
    }
    led_service();

    // the screen yields to the radio, always. a full ssd1306 redraw pushes 1 KB over i2c and
    // blocks for ~25 ms - and at a downlink's ~250 packets a second that is six packets arriving
    // into a fifo three deep. drawing a pretty screen while dropping image data is exactly the
    // wrong trade, and it corrupted every downlink until this line existed
    if (nrf_up && nrf24_pending()) {
        return;
    }
    display_service(lora_packets(), nrf24_packets(), lora_up, nrf_up);
}
