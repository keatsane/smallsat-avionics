/**
 * @file   main.cpp
 * @brief  ground station - receives the vehicle's lora beacon and pumps it out usb serial
 *
 * The whole job is to be a byte pipe. A packet arrives over the air, its bytes go out the usb
 * port, and `tools/uart_monitor.py` decodes them with the same FrameDecoder it already uses on
 * the st-link cable - because the frame format does not change per transport, which is the point
 * REQ-TLM-004 exists to make. Nothing here parses a message.
 *
 * The one thing it does decode is whether a frame passed CRC, and only to blink the led. That
 * uses the satellite's own frame.cpp rather than a second implementation: a ground station with
 * its own hand-written decoder is a second definition of the protocol, and the two drift the
 * first time a message changes.
 *
 * Every modem setting below has to match obc/Src/devices/rfm95.c exactly. A mismatch does not
 * produce an error - it produces silence, which is a far more expensive failure to debug.
 */

#include <Arduino.h>
#include <SPI.h>

#include "protocol/frame.hpp"

// feather m0 rfm9x - the radio is on the board, wired to these pins. VERIFY against adafruit's
// pinout for the exact board: the Feather M0 has them hardwired, but the RadioFruit FeatherWing
// needs solder jumpers, and a wrong cs reads back 0x00 forever with no other symptom
constexpr uint8_t kPinCs = 8;
constexpr uint8_t kPinRst = 4;
constexpr uint8_t kPinDio0 = 3;
constexpr uint8_t kPinLed = 13;

// ---- sx1276 registers, same names as the satellite driver ----
constexpr uint8_t REG_FIFO = 0x00;
constexpr uint8_t REG_OP_MODE = 0x01;
constexpr uint8_t REG_FRF_MSB = 0x06;
constexpr uint8_t REG_FRF_MID = 0x07;
constexpr uint8_t REG_FRF_LSB = 0x08;
constexpr uint8_t REG_FIFO_ADDR_PTR = 0x0D;
constexpr uint8_t REG_FIFO_RX_BASE = 0x0F;
constexpr uint8_t REG_FIFO_RX_CURRENT = 0x10;
constexpr uint8_t REG_IRQ_FLAGS = 0x12;
constexpr uint8_t REG_RX_NB_BYTES = 0x13;
constexpr uint8_t REG_MODEM_CONFIG_1 = 0x1D;
constexpr uint8_t REG_MODEM_CONFIG_2 = 0x1E;
constexpr uint8_t REG_PREAMBLE_MSB = 0x20;
constexpr uint8_t REG_PREAMBLE_LSB = 0x21;
constexpr uint8_t REG_MODEM_CONFIG_3 = 0x26;
constexpr uint8_t REG_SYNC_WORD = 0x39;
constexpr uint8_t REG_VERSION = 0x42;

constexpr uint8_t MODE_LONG_RANGE = 0x80;
constexpr uint8_t MODE_SLEEP = 0x00;
constexpr uint8_t MODE_STDBY = 0x01;
constexpr uint8_t MODE_RX_CONTINUOUS = 0x05;

constexpr uint8_t IRQ_RX_DONE = 0x40;
constexpr uint8_t IRQ_CRC_ERROR = 0x20;

constexpr uint8_t SX1276_VERSION = 0x12;

// 915 MHz in units of F_XOSC / 2^19 = 61.035 Hz - same arithmetic as the satellite
constexpr uint32_t kFrf = static_cast<uint32_t>(915000000.0 / 61.03515625);

// the private sync word, so this hears the rig and not the neighbourhood
constexpr uint8_t kSyncWord = 0x12;

static SPISettings spi_settings(4000000, MSBFIRST, SPI_MODE0);
static fsw::frame_parser_t parser;

static uint8_t reg_read(uint8_t reg) {
    SPI.beginTransaction(spi_settings);
    digitalWrite(kPinCs, LOW);
    SPI.transfer(reg & 0x7F);
    const uint8_t v = SPI.transfer(0x00);
    digitalWrite(kPinCs, HIGH);
    SPI.endTransaction();
    return v;
}

static void reg_write(uint8_t reg, uint8_t val) {
    SPI.beginTransaction(spi_settings);
    digitalWrite(kPinCs, LOW);
    SPI.transfer(reg | 0x80);
    SPI.transfer(val);
    digitalWrite(kPinCs, HIGH);
    SPI.endTransaction();
}

static void set_mode(uint8_t mode) { reg_write(REG_OP_MODE, MODE_LONG_RANGE | mode); }

// true once the radio answers and is listening
static bool radio_begin() {
    pinMode(kPinCs, OUTPUT);
    digitalWrite(kPinCs, HIGH);
    pinMode(kPinDio0, INPUT);

    pinMode(kPinRst, OUTPUT);
    digitalWrite(kPinRst, LOW);
    delay(1);
    digitalWrite(kPinRst, HIGH);
    delay(5);

    SPI.begin();

    if (reg_read(REG_VERSION) != SX1276_VERSION) {
        return false;
    }

    set_mode(MODE_SLEEP);  // the lora bit only latches from sleep
    delay(1);

    reg_write(REG_FRF_MSB, static_cast<uint8_t>(kFrf >> 16));
    reg_write(REG_FRF_MID, static_cast<uint8_t>(kFrf >> 8));
    reg_write(REG_FRF_LSB, static_cast<uint8_t>(kFrf));

    // the whole fifo given to rx, mirroring the satellite giving all of it to tx
    reg_write(REG_FIFO_RX_BASE, 0x00);
    reg_write(REG_FIFO_ADDR_PTR, 0x00);

    // these three are the ones that must match the satellite byte for byte
    reg_write(REG_MODEM_CONFIG_1, 0x72);  // bw125, cr4/5, explicit header
    reg_write(REG_MODEM_CONFIG_2, 0x74);  // sf7, crc on
    reg_write(REG_MODEM_CONFIG_3, 0x04);  // agc on

    reg_write(REG_PREAMBLE_MSB, 0x00);
    reg_write(REG_PREAMBLE_LSB, 0x08);
    reg_write(REG_SYNC_WORD, kSyncWord);

    set_mode(MODE_RX_CONTINUOUS);
    return true;
}

void setup() {
    pinMode(kPinLed, OUTPUT);
    digitalWrite(kPinLed, LOW);

    Serial.begin(115200);
    // deliberately not waiting for a host - a ground station that hangs until someone opens a
    // terminal is a ground station that looks dead when it is fine

    if (!radio_begin()) {
        // the only text this ever prints. it cannot be confused for telemetry: a frame starts
        // AA 55 and the decoder resyncs past anything else
        Serial.println("GSW: lora init failed - check cs/rst wiring and the 3v3 rail");
        while (true) {
            digitalWrite(kPinLed, HIGH);
            delay(100);
            digitalWrite(kPinLed, LOW);
            delay(100);
        }
    }
}

// the led says which of three things is true, because a board that is fine and a board that is
// dead look identical when the only signal is "off". fast blink is init failure (handled in
// setup and never returns); a brief flash every 2 s is alive but hearing nothing; a longer flash
// is a frame that passed crc. non-blocking so nothing here delays a packet
constexpr uint32_t kAliveBlinkMs = 2000;
constexpr uint32_t kAliveFlashMs = 20;
constexpr uint32_t kFrameFlashMs = 60;

static uint32_t led_off_at = 0;
static uint32_t next_alive_blink = 0;

static void led_flash(uint32_t ms) {
    digitalWrite(kPinLed, HIGH);
    led_off_at = millis() + ms;
}

static void led_service() {
    const uint32_t now = millis();
    if (led_off_at != 0 && (int32_t)(now - led_off_at) >= 0) {
        digitalWrite(kPinLed, LOW);
        led_off_at = 0;
    }
    if (led_off_at == 0 && (int32_t)(now - next_alive_blink) >= 0) {
        next_alive_blink = now + kAliveBlinkMs;
        led_flash(kAliveFlashMs);
    }
}

void loop() {
    led_service();

    const uint8_t flags = reg_read(REG_IRQ_FLAGS);
    if ((flags & IRQ_RX_DONE) == 0) {
        return;
    }
    reg_write(REG_IRQ_FLAGS, 0xFF);  // write-1-to-clear

    if ((flags & IRQ_CRC_ERROR) != 0) {
        return;  // the radio's own crc rejected it, so the bytes are not worth passing on
    }

    const uint8_t len = reg_read(REG_RX_NB_BYTES);
    reg_write(REG_FIFO_ADDR_PTR, reg_read(REG_FIFO_RX_CURRENT));

    bool whole_frame = false;
    for (uint8_t i = 0; i < len; i++) {
        const uint8_t b = reg_read(REG_FIFO);
        Serial.write(b);  // straight out, unexamined - this is the byte pipe

        // and the same bytes through the satellite's own decoder, only to prove a frame survived
        if (fsw::frame_decode(&parser, b)) {
            whole_frame = true;
        }
    }

    // a visible flash per good frame, so the box says something without a pc attached. longer
    // than the alive blink so the two are told apart at a glance
    if (whole_frame) {
        led_flash(kFrameFlashMs);
        next_alive_blink = millis() + kAliveBlinkMs;  // do not stack an alive blink on top
    }
}
