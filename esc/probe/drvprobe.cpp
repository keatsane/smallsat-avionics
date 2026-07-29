/**
 * @file   drvprobe.cpp
 * @brief  minimal 6-pwm bring-up - driver only, nothing else, to isolate init failures
 *
 * build/flash with `just esc-probe`. driver.init reporting 0 here means the fault is the
 * toolchain or the library rather than anything in the node firmware.
 */

#include <SimpleFOC.h>

HardwareSerial SerialLink(PB4, PB3);

BLDCDriver6PWM driver =
    BLDCDriver6PWM(A_PHASE_UH, A_PHASE_UL, A_PHASE_VH, A_PHASE_VL, A_PHASE_WH, A_PHASE_WL);

void setup() {
    SerialLink.begin(115200);
    delay(8000);  // time to attach the monitor after a flash

    SimpleFOCDebug::enable(&SerialLink);
    SimpleFOCDebug::println("=== driver probe ===");

    driver.voltage_power_supply = 14.8f;
    driver.voltage_limit = 10.0f;

    int ok = driver.init();
    SimpleFOCDebug::println("driver.init -> ", ok);

    if (ok) {
        driver.enable();
        driver.setPwm(3.0f, 0.0f, 0.0f);  // fixed - should draw current and hold position
        SimpleFOCDebug::println("pwm on phase a");
    }
}

void loop() {}
