/**
 * @file   radios.cpp
 * @brief  rfm95 and nrf24 receivers - see radios.hpp for why the constants are duplicated
 */

#include "radios.hpp"

#include <Arduino.h>
#include <SPI.h>

// ---- pins. the lora three are fixed by the feather itself; the nrf24 two are wiring.md's ----
static constexpr uint8_t kLoraCs = 8;
static constexpr uint8_t kLoraRst = 4;
static constexpr uint8_t kLoraDio0 = 3;
static constexpr uint8_t kNrfCsn = 10;
static constexpr uint8_t kNrfCe = 11;

// ---- sx1276 (lora), same names as obc/Src/devices/rfm95.c ----
static constexpr uint8_t LR_FIFO = 0x00;
static constexpr uint8_t LR_OP_MODE = 0x01;
static constexpr uint8_t LR_FRF_MSB = 0x06;
static constexpr uint8_t LR_FRF_MID = 0x07;
static constexpr uint8_t LR_FRF_LSB = 0x08;
static constexpr uint8_t LR_FIFO_ADDR_PTR = 0x0D;
static constexpr uint8_t LR_FIFO_TX_BASE = 0x0E;
static constexpr uint8_t LR_FIFO_RX_BASE = 0x0F;
static constexpr uint8_t LR_PAYLOAD_LENGTH = 0x22;
static constexpr uint8_t LR_PA_CONFIG = 0x09;
static constexpr uint8_t LR_FIFO_RX_CURRENT = 0x10;
static constexpr uint8_t LR_IRQ_FLAGS = 0x12;
static constexpr uint8_t LR_RX_NB_BYTES = 0x13;
static constexpr uint8_t LR_MODEM_CONFIG_1 = 0x1D;
static constexpr uint8_t LR_MODEM_CONFIG_2 = 0x1E;
static constexpr uint8_t LR_PREAMBLE_MSB = 0x20;
static constexpr uint8_t LR_PREAMBLE_LSB = 0x21;
static constexpr uint8_t LR_MODEM_CONFIG_3 = 0x26;
static constexpr uint8_t LR_SYNC_WORD = 0x39;
static constexpr uint8_t LR_VERSION = 0x42;

static constexpr uint8_t LR_LONG_RANGE = 0x80;
static constexpr uint8_t LR_SLEEP = 0x00;
static constexpr uint8_t LR_STDBY = 0x01;
static constexpr uint8_t LR_TX = 0x03;
static constexpr uint8_t LR_RX_CONTINUOUS = 0x05;
static constexpr uint8_t LR_IRQ_TX_DONE = 0x08;
static constexpr uint8_t LR_IRQ_RX_DONE = 0x40;
static constexpr uint8_t LR_IRQ_CRC_ERROR = 0x20;
static constexpr uint8_t SX1276_VERSION = 0x12;

// 915 MHz in steps of F_XOSC / 2^19 = 61.035 Hz - the satellite's arithmetic
static constexpr uint32_t kFrf = static_cast<uint32_t>(915000000.0 / 61.03515625);
static constexpr uint8_t kLoraSyncWord = 0x12;

// ---- nrf24l01+, same names as obc/Src/devices/nrf24.c ----
static constexpr uint8_t NRF_R_REGISTER = 0x00;
static constexpr uint8_t NRF_W_REGISTER = 0x20;
static constexpr uint8_t NRF_R_RX_PAYLOAD = 0x61;
static constexpr uint8_t NRF_FLUSH_RX = 0xE2;
static constexpr uint8_t NRF_NOP = 0xFF;

static constexpr uint8_t NRF_CONFIG = 0x00;
static constexpr uint8_t NRF_EN_AA = 0x01;
static constexpr uint8_t NRF_EN_RXADDR = 0x02;
static constexpr uint8_t NRF_SETUP_AW = 0x03;
static constexpr uint8_t NRF_SETUP_RETR = 0x04;
static constexpr uint8_t NRF_RF_CH = 0x05;
static constexpr uint8_t NRF_RF_SETUP = 0x06;
static constexpr uint8_t NRF_STATUS = 0x07;
static constexpr uint8_t NRF_RX_ADDR_P0 = 0x0A;
static constexpr uint8_t NRF_RX_PW_P0 = 0x11;
static constexpr uint8_t NRF_FIFO_STATUS = 0x17;
static constexpr uint8_t NRF_RPD = 0x09;  // received power detector, 1 = carrier above ~-64 dBm

// PRIM_RX set - the only difference from the satellite's 0x7E, which is the same word in transmit
static constexpr uint8_t NRF_CONFIG_RX = 0x7F;
static constexpr uint8_t NRF_STATUS_RX_DR = 0x40;
static constexpr uint8_t NRF_FIFO_RX_EMPTY = 0x01;
static constexpr uint8_t kNrfChannel = 76;
// 1 Mbps, minimum transmit power - the satellite's RF_SETUP_1MBPS, byte for byte. this end never
// transmits, so only the data-rate bits matter here; the power bits are matched anyway so the two
// files keep reading the same
static constexpr uint8_t kNrfRfSetup = 0x00;
static constexpr uint8_t kNrfAddrWidth = 5;

// fixed 32-byte packets, because the satellite pads to that. static width means both ends must
// agree in advance; the alternative needs auto-ack, which is off so the vehicle never stalls on
// a ground station that might not be listening
static constexpr uint8_t kNrfPayload = 32;

// "SSAV1" - the same five bytes the satellite transmits to
static const uint8_t kNrfAddress[kNrfAddrWidth] = {0x53, 0x53, 0x41, 0x56, 0x31};

static SPISettings spi_settings(4000000, MSBFIRST, SPI_MODE0);
static uint32_t lora_count = 0;
static uint32_t nrf_count = 0;
static uint32_t nrf_rpd_high = 0;
static uint32_t nrf_rpd_samples = 0;

// ---- shared spi helpers, chip select passed in because two devices share the bus ----

static uint8_t xfer_reg_read(uint8_t cs, uint8_t cmd) {
    SPI.beginTransaction(spi_settings);
    digitalWrite(cs, LOW);
    SPI.transfer(cmd);
    const uint8_t v = SPI.transfer(0x00);
    digitalWrite(cs, HIGH);
    SPI.endTransaction();
    return v;
}

static void xfer_reg_write(uint8_t cs, uint8_t cmd, uint8_t val) {
    SPI.beginTransaction(spi_settings);
    digitalWrite(cs, LOW);
    SPI.transfer(cmd);
    SPI.transfer(val);
    digitalWrite(cs, HIGH);
    SPI.endTransaction();
}

// ---- lora ----

static uint8_t lr_read(uint8_t reg) { return xfer_reg_read(kLoraCs, reg & 0x7F); }
static void lr_write(uint8_t reg, uint8_t val) { xfer_reg_write(kLoraCs, reg | 0x80, val); }
static void lr_mode(uint8_t mode) { lr_write(LR_OP_MODE, LR_LONG_RANGE | mode); }

bool lora_begin() {
    SPI.begin();  // idempotent, and the bus belongs to the radios rather than to main
    pinMode(kLoraCs, OUTPUT);
    digitalWrite(kLoraCs, HIGH);
    pinMode(kLoraDio0, INPUT);

    pinMode(kLoraRst, OUTPUT);
    digitalWrite(kLoraRst, LOW);
    delay(1);
    digitalWrite(kLoraRst, HIGH);
    delay(5);

    if (lr_read(LR_VERSION) != SX1276_VERSION) {
        return false;
    }

    lr_mode(LR_SLEEP);  // the lora bit only latches from sleep
    delay(1);

    lr_write(LR_FRF_MSB, static_cast<uint8_t>(kFrf >> 16));
    lr_write(LR_FRF_MID, static_cast<uint8_t>(kFrf >> 8));
    lr_write(LR_FRF_LSB, static_cast<uint8_t>(kFrf));

    // split the fifo the same way the satellite does: transmit from 0, receive from 0x80
    lr_write(LR_FIFO_TX_BASE, 0x00);
    lr_write(LR_FIFO_RX_BASE, 0x80);
    lr_write(LR_FIFO_ADDR_PTR, 0x00);

    // PA_BOOST at 17 dBm - the feather wires its antenna there, same as the satellite's module
    lr_write(LR_PA_CONFIG, 0x8F);

    // the three that must match the satellite byte for byte
    lr_write(LR_MODEM_CONFIG_1, 0x72);  // bw125, cr4/5, explicit header
    lr_write(LR_MODEM_CONFIG_2, 0x74);  // sf7, crc on
    lr_write(LR_MODEM_CONFIG_3, 0x04);  // agc on

    lr_write(LR_PREAMBLE_MSB, 0x00);
    lr_write(LR_PREAMBLE_LSB, 0x08);
    lr_write(LR_SYNC_WORD, kLoraSyncWord);

    lr_mode(LR_RX_CONTINUOUS);
    return true;
}

void lora_poll(rx_sink_t sink) {
    const uint8_t flags = lr_read(LR_IRQ_FLAGS);
    if ((flags & LR_IRQ_RX_DONE) == 0) {
        return;
    }
    lr_write(LR_IRQ_FLAGS, 0xFF);  // write-1-to-clear

    if ((flags & LR_IRQ_CRC_ERROR) != 0) {
        return;  // the radio's own crc rejected it - the bytes are not worth passing on
    }

    const uint8_t len = lr_read(LR_RX_NB_BYTES);
    lr_write(LR_FIFO_ADDR_PTR, lr_read(LR_FIFO_RX_CURRENT));

    uint8_t buf[64];
    const uint8_t n = (len > sizeof(buf)) ? sizeof(buf) : len;
    for (uint8_t i = 0; i < n; i++) {
        buf[i] = lr_read(LR_FIFO);
    }
    lora_count++;
    sink(buf, n);
}

uint32_t lora_packets() { return lora_count; }

bool lora_send(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0 || len > 255) {
        return false;
    }

    lr_mode(LR_STDBY);
    lr_write(LR_IRQ_FLAGS, 0xFF);
    lr_write(LR_FIFO_ADDR_PTR, 0x00);

    SPI.beginTransaction(spi_settings);
    digitalWrite(kLoraCs, LOW);
    SPI.transfer(LR_FIFO | 0x80);
    for (size_t i = 0; i < len; i++) {
        SPI.transfer(data[i]);
    }
    digitalWrite(kLoraCs, HIGH);
    SPI.endTransaction();

    lr_write(LR_PAYLOAD_LENGTH, static_cast<uint8_t>(len));
    lr_mode(LR_TX);

    // waited on here rather than polled from the loop: a command is a rare, small, deliberate
    // thing, and blocking a few tens of ms for it is simpler than a state machine that has to
    // remember it was mid-transmit. bounded so a dead radio cannot wedge the ground station
    const uint32_t deadline = millis() + 500;
    while ((lr_read(LR_IRQ_FLAGS) & LR_IRQ_TX_DONE) == 0) {
        if (static_cast<int32_t>(millis() - deadline) >= 0) {
            break;
        }
    }
    lr_write(LR_IRQ_FLAGS, 0xFF);
    lr_mode(LR_RX_CONTINUOUS);  // listening is the resting state
    return true;
}

// ---- nrf24 ----

static uint8_t nrf_read(uint8_t reg) { return xfer_reg_read(kNrfCsn, NRF_R_REGISTER | reg); }
static void nrf_write(uint8_t reg, uint8_t val) {
    xfer_reg_write(kNrfCsn, NRF_W_REGISTER | reg, val);
}

static void nrf_cmd(uint8_t c) {
    SPI.beginTransaction(spi_settings);
    digitalWrite(kNrfCsn, LOW);
    SPI.transfer(c);
    digitalWrite(kNrfCsn, HIGH);
    SPI.endTransaction();
}

static void nrf_write_buf(uint8_t reg, const uint8_t* buf, size_t n) {
    SPI.beginTransaction(spi_settings);
    digitalWrite(kNrfCsn, LOW);
    SPI.transfer(NRF_W_REGISTER | reg);
    for (size_t i = 0; i < n; i++) {
        SPI.transfer(buf[i]);
    }
    digitalWrite(kNrfCsn, HIGH);
    SPI.endTransaction();
}

bool nrf24_begin() {
    SPI.begin();
    pinMode(kNrfCsn, OUTPUT);
    digitalWrite(kNrfCsn, HIGH);

    // ce high puts the radio in receive and leaves it there; there is no timed pulse to get wrong
    pinMode(kNrfCe, OUTPUT);
    digitalWrite(kNrfCe, LOW);

    delay(100);  // power-on settle, per the datasheet

    nrf_write(NRF_CONFIG, NRF_CONFIG_RX);
    delay(2);

    // mirrors the satellite: no auto-ack, no retransmits, 5-byte address, channel 76, 1 Mbps
    nrf_write(NRF_EN_AA, 0x00);
    nrf_write(NRF_SETUP_RETR, 0x00);
    nrf_write(NRF_SETUP_AW, 0x03);
    nrf_write(NRF_RF_CH, kNrfChannel);
    nrf_write(NRF_RF_SETUP, kNrfRfSetup);

    // pipe 0 only, listening on the address the satellite transmits to
    nrf_write(NRF_EN_RXADDR, 0x01);
    nrf_write_buf(NRF_RX_ADDR_P0, kNrfAddress, kNrfAddrWidth);
    nrf_write(NRF_RX_PW_P0, kNrfPayload);

    nrf_cmd(NRF_FLUSH_RX);
    nrf_write(NRF_STATUS, 0x70);  // write-1-to-clear the three irq flags

    // no version register on this part - prove it is there by reading back the address just
    // written. five matching bytes is not something a floating bus produces
    uint8_t back[kNrfAddrWidth] = {0};
    SPI.beginTransaction(spi_settings);
    digitalWrite(kNrfCsn, LOW);
    SPI.transfer(NRF_R_REGISTER | NRF_RX_ADDR_P0);
    for (uint8_t i = 0; i < kNrfAddrWidth; i++) {
        back[i] = SPI.transfer(NRF_NOP);
    }
    digitalWrite(kNrfCsn, HIGH);
    SPI.endTransaction();

    for (uint8_t i = 0; i < kNrfAddrWidth; i++) {
        if (back[i] != kNrfAddress[i]) {
            return false;
        }
    }

    digitalWrite(kNrfCe, HIGH);  // listening from here
    return true;
}

void nrf24_poll(rx_sink_t sink) {
    // drain everything waiting, not one packet a pass: a downlink burst arrives far faster than
    // this loop turns over, and the fifo is only three deep
    while ((nrf_read(NRF_FIFO_STATUS) & NRF_FIFO_RX_EMPTY) == 0) {
        uint8_t buf[kNrfPayload];

        SPI.beginTransaction(spi_settings);
        digitalWrite(kNrfCsn, LOW);
        SPI.transfer(NRF_R_RX_PAYLOAD);
        for (uint8_t i = 0; i < kNrfPayload; i++) {
            buf[i] = SPI.transfer(NRF_NOP);
        }
        digitalWrite(kNrfCsn, HIGH);
        SPI.endTransaction();

        nrf_write(NRF_STATUS, NRF_STATUS_RX_DR);  // write-1-to-clear
        nrf_count++;
        sink(buf, kNrfPayload);
    }
}

bool nrf24_pending() {
    // sampled here because this runs every pass, and a downlink burst is gone in ~200 ms - polling
    // the detector once a second alongside the status frame would miss it almost every time
    if ((nrf_read(NRF_RPD) & 0x01) != 0) {
        nrf_rpd_high++;
    }
    nrf_rpd_samples++;
    return (nrf_read(NRF_FIFO_STATUS) & NRF_FIFO_RX_EMPTY) == 0;
}

uint8_t nrf24_channel_busy_pct() {
    const uint32_t total = nrf_rpd_samples;
    const uint32_t high = nrf_rpd_high;
    nrf_rpd_samples = 0;
    nrf_rpd_high = 0;
    if (total == 0) {
        return 0;
    }
    return static_cast<uint8_t>((high * 100U) / total);
}

uint32_t nrf24_packets() { return nrf_count; }
