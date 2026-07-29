/**
 * @file   control_task.cpp
 * @brief  the fixed-rate flight-software cycle - the only task that touches the fsw
 *
 * Everything the flight software sees arrives as an argument to exec.cycle(), so the concurrency
 * stops at this boundary and the executive stays the same single-threaded pure function the host
 * unit tests and SIL run (REQ-PAL-001, REQ-PAL-002, REQ-RT-002, REQ-RT-003).
 */

#include "control_task.hpp"

#include <cstring>

#include "FreeRTOS.h"
#include "console.hpp"
#include "devices/ov2640.h"
#include "devices/ws2812.h"
#include "drivers/gpio.h"
#include "drivers/systick.h"
#include "drivers/uart.h"
#include "fsw/executive.hpp"
#include "protocol/frame.hpp"
#include "rtos_tasks.h"
#include "sensor_task.hpp"
#include "task.h"

namespace {

constexpr uint32_t kCyclePeriodMs = 100U;  // 10 hz control cycle

fsw::Executive exec;
bool camera_ok = false;  // payload camera answered at init

fsw::wheel_status_t wheel{};  // newest status from the esc node
uint32_t wheel_last_ms = 0;   // when it last answered - drives WHEEL_DROPOUT

StaticTask_t s_tcb;
StackType_t s_stack[TASK_STACK_CONTROL];

// drain the esc link and keep the newest wheel status; returns true if one arrived
bool read_wheel_status() {
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
bool read_command(uart_t* u, fsw::frame_parser_t* parser, fsw::command_t* out) {
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

// raw driver reading -> wire/telemetry form. the other three sensors convert in sensor_task.cpp;
// the camera converts here because it is still read from this task - see the note there
fsw::camera_data_t to_camera_data(const ov2640_sample_t& s) {
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
void set_fault_bead(const fsw::FaultManager& fm) {
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

void update_status_leds(const fsw::Executive& e) {
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

// take whatever the sensor task last published into this cycle's inputs.
//
// on a cycle where nothing new arrived the three samples stay unset, which the sensor monitor
// already treats as "not offered this cycle" and skips. that is the honest reading: no sample is
// not the same as a bad sample, and the sensor task falling silent is a task-liveness question for
// the watchdog rather than a sensor fault
void read_sensors(fsw::Inputs& inputs) {
    sensor_set_t set{};
    if (sensor_task_take(&set)) {
        inputs.imu = set.imu;
        inputs.power = set.power;
        inputs.temp = set.temp;
    }

    // the camera is polled like any other device, but its capture also has to be advanced - the
    // arduchip fills the fifo on its own and poll() is what notices that it finished
    if (camera_ok) {
        ov2640_poll(millis());
        inputs.camera = to_camera_data(ov2640_read());
    } else {
        inputs.camera = fsw::camera_data_t{};
    }
}

void control_task(void*) {
    TickType_t next = xTaskGetTickCount();
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

        // absolute wake-up, not a delay: the period stays 100 ms whatever the cycle's work costs,
        // so the rate cannot drift the way a delay-after-work loop does (REQ-RT-002)
        xTaskDelayUntil(&next, pdMS_TO_TICKS(kCyclePeriodMs));
    }
}

}  // namespace

void control_task_create(bool camera_present) {
    camera_ok = camera_present;

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

    xTaskCreateStatic(control_task, "control", TASK_STACK_CONTROL, nullptr, TASK_PRIO_CONTROL,
                      s_stack, &s_tcb);
}
