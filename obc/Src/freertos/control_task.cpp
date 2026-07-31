/**
 * @file   control_task.cpp
 * @brief  the fixed-rate flight-software cycle - the only task that touches the fsw
 *
 * Everything the flight software sees arrives as an argument to exec.cycle(), so the concurrency
 * stops at this boundary and the executive stays the same single-threaded pure function the host
 * unit tests and SIL run (REQ-PAL-001, REQ-PAL-002, REQ-RT-002, REQ-RT-003).
 */

#include "control_task.hpp"

#include <math.h>

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
// the yaw axis is the imu's X, not its Z - measured on the bench 2026-07-30 by spinning the
// platform and watching which channel moved: x swung +/-10000 counts with the spin while z saw
// only tens of cross-coupling. the board is mounted with its x axis vertical. the sign convention
// against the wheel's is still the thing to confirm on the rig: if a detumble command speeds the
// platform up rather than slowing it, this is where to negate
constexpr size_t kYawAxis = 0;

// negated: heading is displayed as a compass, and a compass runs clockwise for an observer
// looking down at the platform - measured 2026-07-30, a clockwise spin read as decreasing
// heading without the flip. this is the single place the human convention enters; the compass
// sign is measured against this downstream, so it follows automatically
constexpr float kGyroRadsPerCount =
    -1.0F / (ICM20948_GYRO_LSB_PER_DPS * 57.29577951F);  // 1/(LSB per dps * dps per rad/s)

// gyro bias, learned at rest. an uncalibrated gyro reads a few counts of offset forever, and
// integrating that walked the heading a quarter degree every ten seconds on the bench - so the
// first kBiasSamples valid readings are averaged and subtracted from everything after. this
// assumes the vehicle is still while BOOT's self-checks run, which is true of a bench rig and is
// the same assumption every consumer imu's startup calibration makes. a vehicle that boots while
// tumbling learns a wrong bias; the recovery is a reset once stable, which SAFE already implies
constexpr uint16_t kBiasSamples = 20;  // 2 s of samples at the 10 Hz sensor rate

// mag field counts -> an absolute yaw heading. with the imu mounted x-vertical the field's y/z
// components live in the horizontal plane, so the heading is their atan2 - a plain compass, tilt
// ignored because the platform never tilts. indoor fields are bent, so this is repeatable rather
// than true north, which is all a bench rig needs.
//
// the raw field is useless without hard-iron calibration, and the first bench run proved it: the
// z component swung around a centre near +300 counts rather than zero, so the uncorrected angle
// swept a small arc while the platform turned 180 degrees. the fix is the standard cheap one -
// track each axis's min and max, take the midpoint as the offset, the half-span as the scale -
// learned live, which means THE COMPASS NEEDS ONE FULL TURN OF THE PLATFORM AFTER POWER-UP
// before it speaks. until then no mag heading is offered and the estimate coasts on the gyro,
// which is the honest degradation.
//
// the offset aligns zero with the camera face: point the camera at the direction that should
// read zero, note what ATTITUDE hdg reports, and set this to minus that reading (in radians)
constexpr float kMagMountOffsetRad = 0.0F;

// spans below this mean the platform has not yet turned far enough to expose the field circle,
// and a heading computed from a sliver of arc is noise dressed as an angle
constexpr int16_t kMagSpanFloor = 100;

// the spike gate: a real field change between two 10 Hz samples is tens of counts even mid-spin;
// hundreds in one step is a glitch. generous so a fast spin is never mistaken for one
constexpr float kMagSpikeCounts = 150.0F;

// smoothing weight per 10 Hz sample - a time constant around 0.3 s
constexpr float kMagEmaAlpha = 0.3F;

// wrap any angle into [-pi, pi] - local copy, the fsw has its own behind the boundary
float wrap_angle(float rad) {
    while (rad > 3.14159265F) {
        rad -= 6.28318531F;
    }
    while (rad < -3.14159265F) {
        rad += 6.28318531F;
    }
    return rad;
}

bool mag_heading_rad(const fsw::imu_data_t& imu, float gyro_rads, float* out) {
    static int16_t min_y = INT16_MAX, max_y = INT16_MIN;
    static int16_t min_z = INT16_MAX, max_z = INT16_MIN;
    static float ema_y = 0.0F, ema_z = 0.0F;
    static bool ema_init = false;

    // two defences the raw stream turned out to need on the bench, where headings repeated to
    // maybe 30 degrees and a returned platform did not read the same angle twice.
    //
    // spike gate first: the mag rides the imu's internal i2c master, and a glitched sample lands
    // as a wild point. min/max calibration never forgets - one spike stretches a span and shifts
    // the centre for the rest of the boot - so a sample too far from the running average is
    // dropped before it can touch anything.
    //
    // then smoothing: the field indoors is noisy at the counts level, and atan2 of a noisy short
    // vector wanders. a light average (about 0.3 s) steadies the angle; the lag it adds during
    // rotation does not matter because the gyro owns motion and the compass only anchors
    const float fy = static_cast<float>(imu.mag[1]);
    const float fz = static_cast<float>(imu.mag[2]);
    if (!ema_init) {
        ema_y = fy;
        ema_z = fz;
        ema_init = true;
    }
    if (fabsf(fy - ema_y) > kMagSpikeCounts || fabsf(fz - ema_z) > kMagSpikeCounts) {
        return false;  // wild point - not for the calibration, not for the heading
    }
    ema_y += kMagEmaAlpha * (fy - ema_y);
    ema_z += kMagEmaAlpha * (fz - ema_z);

    const int16_t y = static_cast<int16_t>(ema_y);
    const int16_t z = static_cast<int16_t>(ema_z);
    min_y = (y < min_y) ? y : min_y;
    max_y = (y > max_y) ? y : max_y;
    min_z = (z < min_z) ? z : min_z;
    max_z = (z > max_z) ? z : max_z;

    const int16_t span_y = static_cast<int16_t>(max_y - min_y);
    const int16_t span_z = static_cast<int16_t>(max_z - min_z);
    if (span_y < kMagSpanFloor || span_z < kMagSpanFloor) {
        return false;  // not calibrated yet - one full turn fixes that
    }

    // hard iron out (midpoint), soft iron roughly out (normalise by half-span) - an ellipse
    // becomes a near-circle, which is as far as a bench rig needs to take it
    const float cy = (static_cast<float>(min_y) + static_cast<float>(max_y)) / 2.0F;
    const float cz = (static_cast<float>(min_z) + static_cast<float>(max_z)) / 2.0F;
    const float ny = (static_cast<float>(y) - cy) / (static_cast<float>(span_y) / 2.0F);
    const float nz = (static_cast<float>(z) - cz) / (static_cast<float>(span_z) / 2.0F);
    const float raw = atan2f(nz, ny);

    // which way round the compass runs is measured, not assumed. hand-picking the sign went
    // wrong twice on this bench - each flip fixed one observation and broke the next - so the
    // firmware now correlates the compass angle's motion against the gyro while the platform
    // turns, and locks whichever sign agrees. until the evidence is decisive there is no mag
    // heading and the estimate coasts on the gyro; a full calibration turn settles it at the
    // same time it fills the hard-iron spans
    static float prev_raw = 0.0F;
    static bool have_prev = false;
    static float corr = 0.0F;
    static int8_t sign = 0;
    constexpr float kCorrDecisive = 2.0F;  // rad*rad/s of agreement before the sign locks

    if (have_prev && sign == 0) {
        corr += gyro_rads * wrap_angle(raw - prev_raw);
        if (corr > kCorrDecisive) {
            sign = 1;
        } else if (corr < -kCorrDecisive) {
            sign = -1;
        }
    }
    prev_raw = raw;
    have_prev = true;

    if (sign == 0) {
        return false;  // direction not yet proven - keep coasting on the gyro
    }
    *out = wrap_angle(static_cast<float>(sign) * raw + kMagMountOffsetRad);
    return true;
}

void read_sensors(fsw::Inputs& inputs) {
    sensor_set_t set{};
    if (sensor_task_take(&set)) {
        inputs.imu = set.imu;
        inputs.power = set.power;
        inputs.temp = set.temp;

        // only when the sample is actually good - a stale or failed read must not be handed over
        // as a rate of zero, which the control law would act on as "already detumbled"
        if ((set.imu.flags & fsw::kImuFlagAccelGyroValid) != 0U) {
            static int32_t bias_sum = 0;
            static uint16_t bias_n = 0;
            static float bias_counts = 0.0F;

            const float raw = static_cast<float>(set.imu.gyro[kYawAxis]);
            if (bias_n < kBiasSamples) {
                bias_sum += set.imu.gyro[kYawAxis];
                bias_n++;
                if (bias_n == kBiasSamples) {
                    bias_counts = static_cast<float>(bias_sum) / static_cast<float>(kBiasSamples);
                }
            }
            inputs.body_rate_rads = (raw - bias_counts) * kGyroRadsPerCount;
        }

        // the absolute half of the attitude picture, offered only when the mag sample is good
        // AND the live hard-iron calibration has seen a full turn - the estimator treats absence
        // as "coast on the gyro", which is the right degradation for both cases
        if ((set.imu.flags & fsw::kImuFlagMagValid) != 0U && inputs.body_rate_rads) {
            float hdg = 0.0F;
            if (mag_heading_rad(set.imu, *inputs.body_rate_rads, &hdg)) {
                inputs.mag_heading_rad = hdg;
            }
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
