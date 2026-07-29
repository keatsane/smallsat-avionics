/**
 * @file   main.cpp
 * @brief  reaction-wheel node - b-g431b-esc1 (stm32g431) running simplefoc in torque mode
 *
 * speaks the same framed, crc-checked protocol as the rest of the system (common/protocol),
 * so the obc link is not a second wire format to maintain. usart2 on pb3/pb4 reaches both
 * the onboard st-link vcp and the j3 header the obc harness lands on (um2516 table 4 - the
 * only usart brought out), so pull the usb cable when the obc is driving.
 */

#include <SimpleFOC.h>

#include "protocol/frame.hpp"
#include "protocol/msg.hpp"

HardwareSerial SerialLink(PB4, PB3);

#define SERIAL_BAUD        115200
#define COMMAND_TIMEOUT_MS 500  // no command for this long -> coast
#define STATUS_PERIOD_MS   250

BLDCMotor motor = BLDCMotor(11);  // 11 pole pairs, confirmed
BLDCDriver6PWM driver =
    BLDCDriver6PWM(A_PHASE_UH, A_PHASE_UL, A_PHASE_VH, A_PHASE_VL, A_PHASE_WH, A_PHASE_WL);
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);

static float target_voltage = 0.0f;  // uq in volts - starts at zero, nothing spins uncommanded
static uint32_t last_command_ms = 0;
static uint16_t last_seq = 0;
static bool foc_ready = false;
static bool sensor_ok = false;
static bool magnet_ok = false;
static bool driver_ok = false;

#define AS5600_ADDR       0x36
#define AS5600_STATUS_REG 0x0B
#define AS5600_MD_BIT     0x20  // magnet detected

// an address ack only proves the chip is alive - MD is what says it can see the magnet
static bool as5600_magnet_detected() {
    Wire.beginTransmission(AS5600_ADDR);
    Wire.write(AS5600_STATUS_REG);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom((uint8_t)AS5600_ADDR, (uint8_t)1) != 1) {
        return false;
    }
    return (Wire.read() & AS5600_MD_BIT) != 0;
}

static fsw::frame_parser_t parser;

static void send_status() {
    fsw::wheel_status_t s{};
    s.velocity_mrad_s = (int32_t)(motor.shaft_velocity * 1000.0f);
    s.angle_mrad = (int32_t)(sensor.getAngle() * 1000.0f);  // raw sensor - true either way
    s.torque_mv = (int16_t)(target_voltage * 1000.0f);
    s.flags =
        (foc_ready ? fsw::kWheelFlagFocReady : 0U) | (sensor_ok ? fsw::kWheelFlagSensorOk : 0U) |
        (magnet_ok ? fsw::kWheelFlagMagnetOk : 0U) | (driver_ok ? fsw::kWheelFlagDriverOk : 0U);
    s.seq = last_seq;

    uint8_t buf[fsw::kFrameMaxSize];
    const size_t n =
        fsw::frame_encode((uint8_t)fsw::MsgId::WheelStatus, (const uint8_t*)&s, sizeof(s), buf);
    SerialLink.write(buf, n);
}

static void apply(const fsw::frame_t& f) {
    if (f.msg_id != (uint8_t)fsw::MsgId::WheelCommand || f.len != sizeof(fsw::wheel_command_t)) {
        return;
    }

    fsw::wheel_command_t c{};
    memcpy(&c, f.payload, sizeof(c));

    float v = (float)c.torque_mv / 1000.0f;
    if (v > motor.voltage_limit) {
        v = motor.voltage_limit;  // the obc does not get to exceed the local ceiling
    } else if (v < -motor.voltage_limit) {
        v = -motor.voltage_limit;
    }

    target_voltage = v;
    last_seq = c.seq;
    last_command_ms = millis();
    send_status();  // every command gets an answer
}

void setup() {
    SerialLink.begin(SERIAL_BAUD);
    delay(1000);

    Wire.setSDA(PB7);
    Wire.setSCL(PB8);
    Wire.begin();

    // address-only probe before anything reads registers - separates a missing encoder
    // from a failed alignment, which look identical from the obc otherwise
    Wire.beginTransmission(AS5600_ADDR);
    sensor_ok = (Wire.endTransmission() == 0);
    magnet_ok = as5600_magnet_detected();

    sensor.init(&Wire);
    motor.linkSensor(&sensor);

    driver.voltage_power_supply = 14.8f;
    driver.voltage_limit = 10.0f;  // hard ceiling
    driver_ok =
        (driver.init() != 0);  // no pwm at all if this fails, which looks like a dead bridge
    motor.linkDriver(&driver);

    // torque (voltage) mode - no pid in the loop
    motor.torque_controller = TorqueControlType::voltage;
    motor.controller = MotionControlType::torque;
    motor.voltage_limit = 8.0f;
    motor.voltage_sensor_align = 6.0f;  // 4.0 was marginal once the flywheel went on

    motor.init();
    foc_ready = (motor.initFOC() != 0);  // reported in the status flags, not assumed

    last_command_ms = millis();
}

void loop() {
    if (foc_ready) {
        motor.loopFOC();
        motor.move(target_voltage);
    } else {
        sensor.update();  // keep the angle live so the encoder can still be checked by hand
    }

    while (SerialLink.available() > 0) {
        auto f = fsw::frame_decode(&parser, (uint8_t)SerialLink.read());
        if (f) {
            apply(*f);
        }
    }

    // dead-man: a hung obc or a pulled cable must not leave the wheel spinning
    if (target_voltage != 0.0f && (millis() - last_command_ms) > COMMAND_TIMEOUT_MS) {
        target_voltage = 0.0f;
        send_status();
    }

    static uint32_t t_last = 0;
    if (millis() - t_last > STATUS_PERIOD_MS) {
        t_last = millis();
        send_status();
    }
}
