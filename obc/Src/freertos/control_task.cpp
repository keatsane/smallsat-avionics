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
#include "devices/icm20948.h"
#include "devices/ov2640.h"
#include "devices/ws2812.h"
#include "drivers/clock.h"
#include "drivers/gpio.h"
#include "drivers/reset.h"
#include "drivers/systick.h"
#include "drivers/uart.h"
#include "fsw/executive.hpp"
#include "protocol/frame.hpp"
#include "rtos_tasks.h"
#include "sensor_task.hpp"
#include "task.h"
#include "task_health.hpp"
#include "uplink_task.hpp"

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

// how many faults are latched, blinked out on bead 1 (REQ-TLM-005). three beads cannot show a
// number any other way, so the bead holds its severity colour and then winks dark once per latched
// fault - the dark gaps are what gets counted, which keeps the colour visible most of the time.
//
// the count includes inhibited faults: they are latched, and "latched" is what the requirement
// asks for. the heartbeat carries the exact set and is the authority; this carries the magnitude,
// and past a handful of faults a human cannot count winks anyway
constexpr uint8_t kBeadHoldCycles = 15U;  // 1.5 s solid - the readable resting state
constexpr uint8_t kBeadWinkCycles = 2U;   // 200 ms dark, then 200 ms lit, per fault

bool fault_bead_lit(uint8_t latched) {
    static uint16_t phase = 0U;
    static uint8_t built_for = 0U;

    // restart whenever the count changes, so a sequence is never a mix of two counts
    if (latched != built_for) {
        built_for = latched;
        phase = 0U;
    }
    if (latched == 0U) {
        return false;
    }

    const uint16_t wink = static_cast<uint16_t>(kBeadWinkCycles) * 2U;
    const uint16_t period = static_cast<uint16_t>(kBeadHoldCycles + (latched * wink));
    const uint16_t p = phase;
    phase = static_cast<uint16_t>((phase + 1U) % period);

    if (p < kBeadHoldCycles) {
        return true;
    }
    // inside the wink train - dark first, so two adjacent holds cannot read as one long one
    return static_cast<uint16_t>((p - kBeadHoldCycles) % wink) >= kBeadWinkCycles;
}

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
    uint8_t latched = 0U;    // every latched fault, inhibited or not
    fsw::Severity worst = fsw::Severity::Warning;

    for (uint8_t i = 0U; i < fsw::kFaultCount; i++) {
        const fsw::Fault f = static_cast<fsw::Fault>(i);
        if (!fm.is_active(f)) {
            continue;
        }
        latched++;
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

    if (!fault_bead_lit(latched)) {
        ws2812_set(1U, 0U, 0U, 0U);  // the dark half of a wink, or genuinely clean
        return;
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
    }
}

// bead 2's "searching" blink - slow and even, so it reads as a state rather than as an alert.
// deliberately the only other bead that moves: the fault bead spends motion on a count, and a
// strip where everything blinks cannot be counted
constexpr uint8_t kLinkBlinkCycles = 5U;  // 500 ms lit, 500 ms dark at the 10 hz cycle

bool link_bead_lit() {
    static uint8_t phase = 0U;
    phase = static_cast<uint8_t>((phase + 1U) % (kLinkBlinkCycles * 2U));
    return phase < kLinkBlinkCycles;
}

void update_status_leds(const fsw::Executive& e, uint32_t t_ms) {
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

    // bead 2 - the uplink: amber blinking while the ground has never been heard from, green in
    // contact, red once contact is lost.
    //
    // the link is asked directly with link_lost() rather than through the fault's response,
    // because response_active() is false while the fault is inhibited - and this build inhibits
    // it. reading the response meant the bead went green on the first command ever received and
    // stayed green for the rest of the session, however long the ground had been gone. the
    // response being suppressed is a decision about safing; it says nothing about the link.
    //
    // "never acquired" is checked first and wins: on a bench with no ground station link_lost()
    // is also true from boot, and never-acquired is the more useful of the two readings
    if (e.commands().log().empty()) {
        // blinking, because this is a state the rig sits in for a whole session - a slow blink
        // reads as searching where a steady amber reads as something settled
        if (link_bead_lit()) {
            ws2812_set(2U, kLedLevel, kLedLevel / 2U, 0U);  // amber
        } else {
            ws2812_set(2U, 0U, 0U, 0U);
        }
    } else if (e.commands().link_lost(t_ms)) {
        ws2812_set(2U, kLedLevel, 0U, 0U);  // red - was in contact, now past the timeout
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
// gyro counts -> rad/s about the yaw axis. this conversion belongs here and not in the flight
// software: it needs the ICM-20948's full-scale setting, and a control law carrying a specific
// part's LSB scaling stops being portable the day the imu changes (REQ-PAL-001).
//
// z is the yaw axis - the one the lazy susan turns about, with the imu mounted flat. the sign
// convention against the wheel's is the thing to confirm on the rig: if a detumble command speeds
// the platform up rather than slowing it, this is where to negate
constexpr float kGyroRadsPerCount =
    1.0F / (ICM20948_GYRO_LSB_PER_DPS * 57.29577951F);  // 1/(LSB per dps * dps per rad/s)

void read_sensors(fsw::Inputs& inputs) {
    sensor_set_t set{};
    if (sensor_task_take(&set)) {
        inputs.imu = set.imu;
        inputs.power = set.power;
        inputs.temp = set.temp;

        // only when the sample is actually good - a stale or failed read must not be handed over
        // as a rate of zero, which the control law would act on as "already detumbled"
        if ((set.imu.flags & fsw::kImuFlagAccelGyroValid) != 0U) {
            inputs.body_rate_rads = static_cast<float>(set.imu.gyro[2]) * kGyroRadsPerCount;
        }
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

// the driver reads RCC_CSR into its own enum and the wire has its own catalog; these assert the
// two are the same numbering rather than leaving a silent mapping to drift. a reordered list in
// either file fails the build here instead of mislabelling a reset on the ground
static_assert(static_cast<uint8_t>(fsw::ResetCause::UNKNOWN) == RESET_CAUSE_UNKNOWN, "");
static_assert(static_cast<uint8_t>(fsw::ResetCause::POWER_ON) == RESET_CAUSE_POWER_ON, "");
static_assert(static_cast<uint8_t>(fsw::ResetCause::RESET_PIN) == RESET_CAUSE_PIN, "");
static_assert(static_cast<uint8_t>(fsw::ResetCause::BROWNOUT) == RESET_CAUSE_BROWNOUT, "");
static_assert(static_cast<uint8_t>(fsw::ResetCause::SOFTWARE) == RESET_CAUSE_SOFTWARE, "");
static_assert(static_cast<uint8_t>(fsw::ResetCause::WATCHDOG) == RESET_CAUSE_IWDG, "");
static_assert(static_cast<uint8_t>(fsw::ResetCause::WINDOW_WATCHDOG) == RESET_CAUSE_WWDG, "");
static_assert(static_cast<uint8_t>(fsw::ResetCause::LOWPOWER) == RESET_CAUSE_LOWPOWER, "");

void control_task(void*) {
    TickType_t next = xTaskGetTickCount();
    uint32_t cycle = 0U;

    for (;;) {
        fsw::Inputs inputs{};
        read_sensors(inputs);

        // once, on the first cycle - the executive turns it into a frame (REQ-WDG-002)
        if (cycle == 0U) {
            fsw::boot_info_t b{};
            b.t_ms = millis();
            b.clk_hz = clock_hclk_hz();
            b.reset_cause = static_cast<uint8_t>(reset_cause());
            inputs.boot = b;
        }

        // drain the esc link before the cycle - a status this pass is what clears WHEEL_DROPOUT
        if (read_wheel_status()) {
            if (wheel_last_ms == 0U) {
                console_printf("ESC: up at %lu ms, flags=0x%02X\r\n",
                               static_cast<unsigned long>(millis()), wheel.flags);
            }
            wheel_last_ms = millis();
            inputs.wheel = wheel;
        }

        // one command per cycle, because Inputs carries one. the uplink task holds the rest until
        // the cycles that can take them, so a burst is delayed rather than dropped
        fsw::command_t cmd{};
        if (uplink_task_take(&cmd)) {
            inputs.command = cmd;
        }

        exec.cycle(inputs, millis());

        if ((cycle % 10U) == 0U) {
            ld2_toggle();  // ~1 hz alive blink
        }

        // TEMP esc rx diagnosis - are bytes arriving at all, and are they clean. printed once and
        // then only when something moves: the question is "has anything changed", and repeating
        // the same all-zero line every two seconds forever only buries the lines that matter
        if ((cycle % 20U) == 0U && wheel_last_ms == 0U) {
            static uint32_t last_sum = UINT32_MAX;
            const uart_errors_t e = uart_get_errors(uart_esc);
            const uint32_t waiting = static_cast<uint32_t>(uart_rx_available(uart_esc));
            const uint32_t sum = waiting + e.overrun + e.framing + e.noise + e.dropped;
            if (sum != last_sum) {
                last_sum = sum;
                console_printf(
                    "ESC rx: waiting=%lu ore=%lu fe=%lu ne=%lu drop=%lu\r\n",
                    static_cast<unsigned long>(waiting), static_cast<unsigned long>(e.overrun),
                    static_cast<unsigned long>(e.framing), static_cast<unsigned long>(e.noise),
                    static_cast<unsigned long>(e.dropped));
            }
        }

        update_status_leds(exec, millis());
        ws2812_show();  // re-send every cycle - a missed latch self-heals next pass

        cycle++;

        task_health_checkin(TASK_ID_CONTROL);

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

    const TaskHandle_t h = xTaskCreateStatic(control_task, "control", TASK_STACK_CONTROL, nullptr,
                                             TASK_PRIO_CONTROL, s_stack, &s_tcb);
    // five missed cycles. tight enough that a wedged control loop resets the vehicle quickly,
    // loose enough that the worst jitter measured on the bench (~10 ms during a downlink) is
    // nowhere near it
    task_health_register(TASK_ID_CONTROL, h, 500U);
}
