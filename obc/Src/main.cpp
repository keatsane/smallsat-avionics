/**
 * @file   main.cpp
 * @brief  node entry - bring up the board, then run the flight-software cycle
 */

#include <cstdarg>  // console_printf
#include <cstdio>
#include <cstring>

#include "devices/icm20948.h"
#include "devices/ina228.h"
#include "devices/ov2640.h"
#include "devices/tmp117.h"
#include "devices/ws2812.h"
#include "drivers/clock.h"
#include "drivers/gpio.h"
#include "drivers/i2c.h"
#include "drivers/reset.h"
#include "drivers/spi.h"
#include "drivers/systick.h"
#include "drivers/uart.h"
#include "fsw/executive.hpp"
#include "fsw/platform.hpp"
#include "protocol/frame.hpp"

namespace {

fsw::Executive exec;
bool imu_ok = false;     // did the imu answer at init - gates reads below
bool power_ok = false;   // power monitor
bool temp_ok = false;    // temperature sensor
bool camera_ok = false;  // payload camera

fsw::wheel_status_t wheel{};  // newest status from the esc node
uint32_t wheel_last_ms = 0;   // when it last answered - drives WHEEL_DROPOUT

}  // namespace

// console helpers - length comes from the string, not a hand-counted one
static void console_puts(const char* s) {
    uart_write(uart_console, reinterpret_cast<const uint8_t*>(s), std::strlen(s));
}

static void console_printf(const char* fmt, ...) {
    char line[96];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    // vsnprintf returns what it wanted to write, not what fit
    size_t len = static_cast<size_t>(n);
    if (len >= sizeof(line)) {
        len = sizeof(line) - 1U;
    }
    uart_write(uart_console, reinterpret_cast<const uint8_t*>(line), len);
}

// drain the esc link and keep the newest wheel status; returns true if one arrived
static bool read_wheel_status() {
    static fsw::frame_parser_t parser;
    bool got = false;
    uint8_t b = 0U;

    while (uart_read_byte(uart_esc, &b)) {
        auto f = fsw::frame_decode(&parser, b);
        if (f && f->msg_id == static_cast<uint8_t>(fsw::MsgId::WheelStatus) &&
            f->len == sizeof(fsw::wheel_status_t)) {
            std::memcpy(&wheel, f->payload, sizeof(wheel));
            got = true;
        }
    }
    return got;
}

// drain one uplink and keep the newest command; returns true if one arrived.
//
// only the newest survives a cycle, because Inputs carries one command - a burst arriving inside
// a single 100 ms cycle loses all but the last. that is acceptable rather than overlooked: every
// command is acked (REQ-CMD-003), so a ground station sees the gap and resends. it stops being
// acceptable the moment anything scripts commands faster than the cycle rate
static bool read_command(uart_t* u, fsw::frame_parser_t* parser, fsw::command_t* out) {
    bool got = false;
    uint8_t b = 0U;

    while (uart_read_byte(u, &b)) {
        auto f = fsw::frame_decode(parser, b);
        if (f && f->msg_id == static_cast<uint8_t>(fsw::MsgId::Command) &&
            f->len == sizeof(fsw::command_t)) {
            std::memcpy(out, f->payload, sizeof(*out));
            got = true;
        }
    }
    return got;
}

// bring up clocks, the time base, and peripherals; record which sensors came up
static void init(void) {
    reset_init();  // latch why we last reset before anything can trigger a new one
    clock_init();
    systick_init();
    ld2_init();
    ws2812_init();
    uart_esc_init();
    uart_console_init();   // console: usart2 -> st-link vcp (usb)
    uart_downlink_init();  // downlink: usart6 -> pc6/pc7 header

    // boot banner - why we reset + the live core clock
    console_printf("BOOT: reset=%s clk=%lu Hz\r\n", reset_cause_str(reset_cause()),
                   static_cast<unsigned long>(clock_hclk_hz()));

    // sensors take longer than the mcu to come up from cold - without this their inits silently
    // fail on power-on (a pin reset works, the rails are already stable)
    delay_ms(100U);

    imu_ok = icm20948_init();

    i2c_sensors_init();

    // bus scan - every address that acks, before any driver touches the bus.
    // expect 0x30 (camera sccb), 0x40 (ina228), 0x48 (tmp117)
    console_puts("I2C scan:");
    for (uint8_t addr = 0x08U; addr < 0x78U; addr++) {
        if (i2c_probe(i2c_sensors, addr) == I2C_OK) {
            console_printf(" 0x%02X", addr);
        }
    }
    console_puts("\r\n");

    power_ok = ina228_init();
    temp_ok = tmp117_init();

    if (!imu_ok) {
        console_puts("IMU INIT FAIL\r\n");
    } else {
        // boot gyro bias - should land near the raw zero-rate offset, not near zero
        int16_t bias[3];
        icm20948_gyro_bias(bias);
        console_printf("IMU gyro bias: %d %d %d\r\n", bias[0], bias[1], bias[2]);
    }

    if (!power_ok) {
        console_puts("POWER INIT FAIL\r\n");
    }

    if (!temp_ok) {
        console_puts("TEMP INIT FAIL\r\n");
    }

    camera_ok = ov2640_init();
    if (!camera_ok) {
        console_puts("CAMERA INIT FAIL\r\n");
    }

    // BENCH INHIBITS - subsystems that are physically absent from the rig right now. an inhibited
    // fault still debounces, latches, logs, and appears in the heartbeat; only its autonomous
    // response is suppressed, so no run can be read as clean when it was not. empty this list
    // as the hardware comes back (REQ-FAULT-012)
    static const fsw::Fault kBenchInhibits[] = {
        fsw::Fault::COMMAND_LINK_LOSS,  // no ground station until phase 8
        fsw::Fault::WHEEL_DROPOUT,      // esc removed from the stack pending a replacement board
    };

    for (const fsw::Fault f : kBenchInhibits) {
        exec.inhibit_fault(f);
    }

    // loud, and first in every log - the banner is the reason inhibits cannot quietly become a lie
    console_puts("BENCH BUILD - fault responses inhibited: ");
    for (size_t i = 0U; i < (sizeof(kBenchInhibits) / sizeof(kBenchInhibits[0])); i++) {
        console_printf("%s%s", (i > 0U) ? ", " : "",
                       fsw::fault_name(static_cast<uint8_t>(kBenchInhibits[i])));
    }
    console_puts("\r\n");

    // no esc check here - it spends several seconds aligning foc before it says anything.
    // the main loop reports the link the moment it comes up
    fsw::platform::set_wheel_torque(0);
}

// raw driver reading -> wire/telemetry form (the one place the bsp and fsw types meet)
static fsw::imu_data_t to_imu_data(const icm20948_sample_t& s) {
    fsw::imu_data_t d{};
    d.t_ms = s.t_ms;
    for (size_t i = 0U; i < 3U; i++) {
        d.accel[i] = s.accel[i];
        d.gyro[i] = s.gyro[i];
        d.mag[i] = s.mag[i];
    }
    d.flags = static_cast<uint8_t>((s.accel_gyro_valid ? fsw::kImuFlagAccelGyroValid : 0U) |
                                   (s.mag_valid ? fsw::kImuFlagMagValid : 0U));
    return d;
}

static fsw::power_data_t to_power_data(const ina228_sample_t& s) {
    fsw::power_data_t d{};
    d.t_ms = s.t_ms;
    d.bus_mv = s.bus_mv;
    d.current_ma = s.current_ma;
    d.power_mw = s.power_mw;
    d.flags = static_cast<uint8_t>(s.valid ? fsw::kPowerFlagValid : 0U);
    return d;
}

static fsw::temp_data_t to_temp_data(const tmp117_sample_t& s) {
    fsw::temp_data_t d{};
    d.t_ms = s.t_ms;
    d.temp_mc = s.temp_mc;
    d.flags = static_cast<uint8_t>(s.valid ? fsw::kTempFlagValid : 0U);
    return d;
}

static fsw::camera_data_t to_camera_data(const ov2640_sample_t& s) {
    fsw::camera_data_t d{};
    d.t_ms = s.t_ms;
    d.frame_bytes = s.frame_bytes;
    d.flags = static_cast<uint8_t>(
        (s.valid ? fsw::kCameraFlagValid : 0U) | (s.frame_ready ? fsw::kCameraFlagFrameReady : 0U) |
        ((s.state == OV2640_CAPTURING) ? fsw::kCameraFlagCapturing : 0U));
    return d;
}

// led policy lives here, not in the fsw - the driver just takes colors (REQ-HMI-001)
// low on purpose - the beads sit behind printed plastic and bleed through the walls at anything
// brighter. this is the floor: the mixed colors (orange, amber, cyan) set one channel to a
// fraction of it, and below 4 there are not enough steps left down there to hold the hue
constexpr uint8_t kLedLevel = 4U;

// bead 1 - the fault ladder, worst rung wins: red critical, orange degraded, yellow warning, then
// blue for faults that are latched but inhibited, then off when genuinely clean.
//
// an inhibited fault is kept OUT of the severity ladder on purpose. it is still latched, still in
// the heartbeat, and still in the boot banner - but the bench runs with subsystems deliberately
// absent, and letting those paint the bead red on every build is how red stops meaning anything.
// blue sits below warning because it is the one rung that is not an alarm: it says "something is
// latched and we already decided not to act on it"
static void set_fault_bead(const fsw::FaultManager& fm) {
    bool acting = false;     // at least one latched fault whose response is live
    bool inhibited = false;  // at least one latched fault whose response is suppressed
    fsw::Severity worst = fsw::Severity::Warning;

    for (uint8_t i = 0U; i < fsw::kFaultCount; i++) {
        const fsw::Fault f = static_cast<fsw::Fault>(i);
        if (!fm.is_active(f)) {
            continue;
        }
        if (fm.is_inhibited(f)) {
            inhibited = true;
            continue;
        }
        const fsw::Severity s = fm.fault_spec(f).severity;
        if (!acting || s > worst) {
            worst = s;
            acting = true;
        }
    }

    if (acting) {
        if (worst == fsw::Severity::Critical) {
            ws2812_set(1U, kLedLevel, 0U, 0U);  // red
        } else if (worst == fsw::Severity::Degraded) {
            ws2812_set(1U, kLedLevel, kLedLevel / 4U, 0U);  // orange
        } else {
            ws2812_set(1U, kLedLevel, kLedLevel, 0U);  // yellow
        }
    } else if (inhibited) {
        ws2812_set(1U, 0U, 0U, kLedLevel);  // blue - latched, deliberately not acted on
    } else {
        ws2812_set(1U, 0U, 0U, 0U);
    }
}

static void update_status_leds(const fsw::Executive& e) {
    switch (e.modes().mode()) {  // bead 0 - mode
        case fsw::Mode::BOOT:
            ws2812_set(0U, kLedLevel, kLedLevel, kLedLevel);  // white
            break;
        case fsw::Mode::STANDBY:
            ws2812_set(0U, 0U, kLedLevel, 0U);  // green
            break;
        case fsw::Mode::DETUMBLE:
            ws2812_set(0U, 0U, 0U, kLedLevel);  // blue
            break;
        case fsw::Mode::POINTING:
            ws2812_set(0U, 0U, kLedLevel, kLedLevel);  // cyan
            break;
        case fsw::Mode::DOWNLINK:
            ws2812_set(0U, kLedLevel, 0U, kLedLevel);  // magenta
            break;
        case fsw::Mode::SAFE:
            ws2812_set(0U, kLedLevel, 0U, 0U);  // red
            break;
    }

    set_fault_bead(e.faults());

    // bead 2 - uplink. amber until the first command arrives - no loss fault yet only
    // means the timer has not expired. with the loss fault inhibited this falls through to amber,
    // which is the truthful reading on a bench with no ground station: never acquired, not lost
    if (e.faults().response_active(fsw::Fault::COMMAND_LINK_LOSS)) {
        ws2812_set(2U, kLedLevel, 0U, 0U);  // red - lost
    } else if (e.commands().log().empty()) {
        ws2812_set(2U, kLedLevel, kLedLevel / 2U, 0U);  // amber - never heard from the ground
    } else {
        ws2812_set(2U, 0U, kLedLevel, 0U);  // green - in contact
    }
}

// read every available sensor into this cycle's inputs
static void read_sensors(fsw::Inputs& inputs) {
    // always hand the fsw a sample so the sensor monitor can judge it, defaults to empty (invalid)
    inputs.imu = imu_ok ? to_imu_data(icm20948_read()) : fsw::imu_data_t{};
    inputs.power = power_ok ? to_power_data(ina228_read()) : fsw::power_data_t{};
    inputs.temp = temp_ok ? to_temp_data(tmp117_read()) : fsw::temp_data_t{};

    // the camera is polled like any other device, but its capture also has to be advanced - the
    // arduchip fills the fifo on its own and poll() is what notices that it finished
    if (camera_ok) {
        ov2640_poll(millis());
        inputs.camera = to_camera_data(ov2640_read());
    } else {
        inputs.camera = fsw::camera_data_t{};
    }
}

int main(void) {
    init();

    uint32_t cycle = 0U;
    for (;;) {
        fsw::Inputs inputs{};
        read_sensors(inputs);

        // drain the esc link before the cycle - a status this pass is what clears WHEEL_DROPOUT
        if (read_wheel_status()) {
            if (wheel_last_ms == 0U) {
                console_printf("ESC: up at %lu ms, flags=0x%02X\r\n",
                               static_cast<unsigned long>(millis()), wheel.flags);
            }
            wheel_last_ms = millis();
            inputs.wheel = wheel;
        }

        // drain both uplinks - the ground can reach the obc over the st-link vcp on the bench or
        // the pc6/pc7 header the radio will land on, and telemetry already goes out both. drain
        // each unconditionally rather than short-circuiting, or the quiet one's ring fills up
        {
            static fsw::frame_parser_t console_uplink;
            static fsw::frame_parser_t downlink_uplink;
            fsw::command_t cmd{};
            const bool from_console = read_command(uart_console, &console_uplink, &cmd);
            const bool from_radio = read_command(uart_downlink, &downlink_uplink, &cmd);
            if (from_console || from_radio) {
                inputs.command = cmd;
            }
        }

        exec.cycle(inputs, millis());

        if ((cycle % 10U) == 0U) {
            ld2_toggle();  // ~1 hz alive blink
        }

        // TEMP esc rx diagnosis - are bytes arriving at all, and are they clean
        if ((cycle % 20U) == 0U && wheel_last_ms == 0U) {
            uart_errors_t e = uart_get_errors(uart_esc);
            console_printf(
                "ESC rx: waiting=%u ore=%lu fe=%lu ne=%lu drop=%lu\r\n",
                static_cast<unsigned>(uart_rx_available(uart_esc)),
                static_cast<unsigned long>(e.overrun), static_cast<unsigned long>(e.framing),
                static_cast<unsigned long>(e.noise), static_cast<unsigned long>(e.dropped));
        }

        update_status_leds(exec);
        ws2812_show();  // re-send every cycle - a missed latch self-heals next pass

        cycle++;
        delay_ms(100U);  // 10 hz cycle
    }
}
