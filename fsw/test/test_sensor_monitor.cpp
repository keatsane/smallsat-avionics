#include "doctest.h"
#include "fsw/sensor_monitor.hpp"

using namespace fsw;

namespace {
// inputs carrying one imu sample - every axis set to v, with the given flag bits
Inputs imu_reading(int16_t v, uint8_t flags) {
    Inputs inputs;
    imu_data_t s{};
    for (int i = 0; i < 3; ++i) {
        s.accel[i] = v;
        s.gyro[i] = v;
        s.mag[i] = v;
    }
    s.flags = flags;
    inputs.imu = s;
    return inputs;
}

constexpr uint8_t kBothValid = kImuFlagAccelGyroValid | kImuFlagMagValid;

// inputs carrying one power sample; defaults are a healthy 5 V rail - override one field per test
Inputs power_reading(uint32_t bus_mv = 5000, int32_t current_ma = 150, int16_t dietemp_cc = 2500,
                     uint8_t flags = kPowerFlagValid) {
    Inputs inputs;
    power_data_t s{};
    s.bus_mv = bus_mv;
    s.current_ma = current_ma;
    s.dietemp_cc = dietemp_cc;
    s.flags = flags;
    inputs.power = s;
    return inputs;
}
}  // namespace

TEST_SUITE("SENSOR MONITOR REQUIREMENTS") {
    TEST_CASE("REQ-SNS-002") {
        SUBCASE("A healthy, changing stream latches nothing") {
            SensorMonitor sm;
            FaultManager fm;

            for (uint32_t t = 0; t <= 1000; t += 100) {
                sm.evaluate(imu_reading(static_cast<int16_t>(t), kBothValid), fm, t);
            }

            CHECK_FALSE(fm.is_active(Fault::ACCEL_GYRO_DROPOUT));
            CHECK_FALSE(fm.is_active(Fault::MAG_DROPOUT));
        }

        SUBCASE("An invalid sample latches both dropouts after debounce") {
            SensorMonitor sm;
            FaultManager fm;
            const Inputs invalid = imu_reading(0, 0);  // both validity bits clear

            sm.evaluate(invalid, fm, 0);
            sm.evaluate(invalid, fm, 1);
            CHECK_FALSE(
                fm.is_active(Fault::ACCEL_GYRO_DROPOUT));  // still below the debounce threshold

            sm.evaluate(invalid, fm, 2);  // third consecutive bad sample
            CHECK(fm.is_active(Fault::ACCEL_GYRO_DROPOUT));
            CHECK(fm.is_active(Fault::MAG_DROPOUT));
        }

        SUBCASE("A cleared valid bit drops only the gyro half") {
            SensorMonitor sm;
            FaultManager fm;
            const Inputs mag_only = imu_reading(0, kImuFlagMagValid);  // mag good, accel/gyro bad

            for (uint32_t t = 0; t < 3; ++t) {
                sm.evaluate(mag_only, fm, t);
            }

            CHECK(fm.is_active(Fault::ACCEL_GYRO_DROPOUT));
            CHECK_FALSE(fm.is_active(Fault::MAG_DROPOUT));
        }

        SUBCASE("A cleared mag bit drops only the mag half") {
            SensorMonitor sm;
            FaultManager fm;
            const Inputs ag_only = imu_reading(0, kImuFlagAccelGyroValid);  // accel/gyro good

            for (uint32_t t = 0; t < 3; ++t) {
                sm.evaluate(ag_only, fm, t);
            }

            CHECK(fm.is_active(Fault::MAG_DROPOUT));
            CHECK_FALSE(fm.is_active(Fault::ACCEL_GYRO_DROPOUT));
        }

        SUBCASE("No sample is no opinion - absence never latches") {
            SensorMonitor sm;
            FaultManager fm;
            const Inputs none;  // no imu set -> nullopt

            for (uint32_t t = 0; t < 10; ++t) {
                sm.evaluate(none, fm, t);
            }

            CHECK_FALSE(fm.is_active(Fault::ACCEL_GYRO_DROPOUT));
            CHECK_FALSE(fm.is_active(Fault::MAG_DROPOUT));
        }

        SUBCASE("A good sample resets the dropout streak") {
            SensorMonitor sm;
            FaultManager fm;
            const Inputs dropped = imu_reading(0, 0);
            const Inputs good = imu_reading(5, kBothValid);

            sm.evaluate(dropped, fm, 0);
            sm.evaluate(dropped, fm, 1);  // 2 of 3 bad
            sm.evaluate(good, fm, 2);     // good sample resets the streak
            sm.evaluate(dropped, fm, 3);  // streak restarts

            CHECK_FALSE(fm.is_active(Fault::ACCEL_GYRO_DROPOUT));
        }

        SUBCASE("A frozen but valid reading latches the matching dropouts") {
            SensorMonitor sm;
            FaultManager fm;
            const Inputs frozen = imu_reading(100, kBothValid);  // same value every cycle

            sm.evaluate(frozen, fm, 0);    // changes from the initial state - fresh
            sm.evaluate(frozen, fm, 600);  // unchanged past the window - dropout #1
            sm.evaluate(frozen, fm, 700);  // dropout #2
            CHECK_FALSE(fm.is_active(Fault::ACCEL_GYRO_DROPOUT));  // below the debounce threshold
            CHECK_FALSE(fm.is_active(Fault::MAG_DROPOUT));

            sm.evaluate(frozen, fm, 800);  // dropout #3 - latches
            // REQ-SNS-002: a freeze raises the matching dropout (data is unusable)
            CHECK(fm.is_active(Fault::ACCEL_GYRO_DROPOUT));
            CHECK(fm.is_active(Fault::MAG_DROPOUT));
        }

        SUBCASE("A dropped sensor is just a dropout") {
            SensorMonitor sm;
            FaultManager fm;
            const Inputs dropped = imu_reading(0, 0);  // invalid, and held frozen past the window

            sm.evaluate(dropped, fm, 0);
            sm.evaluate(dropped, fm, 600);
            sm.evaluate(dropped, fm, 700);
            sm.evaluate(dropped, fm, 800);

            CHECK(fm.is_active(Fault::ACCEL_GYRO_DROPOUT));
            CHECK(fm.is_active(Fault::MAG_DROPOUT));
        }
    }

    TEST_CASE("REQ-SNS-004") {
        SUBCASE("A healthy rail latches nothing") {
            SensorMonitor sm;
            FaultManager fm;
            for (uint32_t t = 0; t < 10; ++t) {
                sm.evaluate(power_reading(), fm, t);
            }
            CHECK_FALSE(fm.is_active(Fault::UNDERVOLTAGE));
            CHECK_FALSE(fm.is_active(Fault::OVERVOLTAGE));
            CHECK_FALSE(fm.is_active(Fault::OVERCURRENT));
            CHECK_FALSE(fm.is_active(Fault::OVERTEMPERATURE));
            CHECK_FALSE(fm.is_active(Fault::UNDERTEMPERATURE));
        }

        SUBCASE("Undervoltage latches after the debounce threshold") {
            SensorMonitor sm;
            FaultManager fm;
            const Inputs low = power_reading(4000);  // below 4.5 V

            sm.evaluate(low, fm, 0);
            sm.evaluate(low, fm, 1);
            CHECK_FALSE(fm.is_active(Fault::UNDERVOLTAGE));  // below the debounce threshold (3)
            sm.evaluate(low, fm, 2);
            CHECK(fm.is_active(Fault::UNDERVOLTAGE));
        }

        SUBCASE("Overvoltage latches") {
            SensorMonitor sm;
            FaultManager fm;
            for (uint32_t t = 0; t < 3; ++t) {
                sm.evaluate(power_reading(6000), fm, t);  // above 5.5 V
            }
            CHECK(fm.is_active(Fault::OVERVOLTAGE));
        }

        SUBCASE("Overcurrent latches") {
            SensorMonitor sm;
            FaultManager fm;
            for (uint32_t t = 0; t < 3; ++t) {
                sm.evaluate(power_reading(5000, 500), fm, t);  // above 300 mA
            }
            CHECK(fm.is_active(Fault::OVERCURRENT));
        }

        SUBCASE("Overtemperature latches after its longer debounce") {
            SensorMonitor sm;
            FaultManager fm;
            const Inputs hot = power_reading(5000, 150, 9000);  // above 80 degC

            for (uint32_t t = 0; t < 4; ++t) {
                sm.evaluate(hot, fm, t);
            }
            CHECK_FALSE(fm.is_active(Fault::OVERTEMPERATURE));  // below the debounce threshold (5)
            sm.evaluate(hot, fm, 4);
            CHECK(fm.is_active(Fault::OVERTEMPERATURE));
        }

        SUBCASE("Undertemperature latches") {
            SensorMonitor sm;
            FaultManager fm;
            const Inputs cold = power_reading(5000, 150, -3000);  // below -20 degC
            for (uint32_t t = 0; t < 5; ++t) {
                sm.evaluate(cold, fm, t);
            }
            CHECK(fm.is_active(Fault::UNDERTEMPERATURE));
        }

        SUBCASE("An invalid sample is no opinion - out-of-range values never latch") {
            SensorMonitor sm;
            FaultManager fm;
            const Inputs invalid =
                power_reading(4000, 500, -3000, 0);  // every value bad, but flag clear
            for (uint32_t t = 0; t < 10; ++t) {
                sm.evaluate(invalid, fm, t);
            }
            CHECK_FALSE(fm.is_active(Fault::UNDERVOLTAGE));
            CHECK_FALSE(fm.is_active(Fault::OVERCURRENT));
            CHECK_FALSE(fm.is_active(Fault::UNDERTEMPERATURE));
        }

        SUBCASE("No sample is no opinion") {
            SensorMonitor sm;
            FaultManager fm;
            const Inputs none;  // no power set -> nullopt
            for (uint32_t t = 0; t < 10; ++t) {
                sm.evaluate(none, fm, t);
            }
            CHECK_FALSE(fm.is_active(Fault::UNDERVOLTAGE));
        }
    }
}
