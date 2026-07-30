/**
 * @file   sensor_task.cpp
 * @brief  sensor sampling, off the control loop and behind a queue
 *
 * The reads here block: i2c waits on the peripheral's flags for every byte, and the imu's spi
 * transfer is polled too. In the super-loop that stall was simply part of the cycle, which is
 * exactly what a fixed-rate control loop must not carry. Here the buses hold up a lower-priority
 * task and the control cycle takes whatever was last published.
 *
 * Nothing else touches these two buses, so no mutex: spi2 belongs to the imu and i2c1 to the
 * power/temperature pair. The camera is deliberately NOT here - it shares spi3 with the payload
 * downlink that the control task drives, and that bus needs a mutex before a second task reads it.
 */

#include "sensor_task.hpp"

#include "FreeRTOS.h"
#include "devices/icm20948.h"
#include "devices/ina228.h"
#include "devices/tmp117.h"
#include "queue.h"
#include "rtos_tasks.h"
#include "task.h"
#include "task_health.hpp"

namespace {

constexpr uint32_t kSamplePeriodMs = 100U;  // matches the control cycle - one fresh set per cycle

bool s_imu_ok = false;
bool s_power_ok = false;
bool s_temp_ok = false;

// length 1, overwritten rather than queued. a sample the control cycle did not get to is not
// worth keeping: it has already been superseded by a newer reading of the same thing, and a
// backlog of stale sensor state is how a control loop ends up acting on the past
QueueHandle_t s_queue;
StaticQueue_t s_queue_ctrl;
uint8_t s_queue_storage[sizeof(sensor_set_t)];

StaticTask_t s_tcb;
StackType_t s_stack[TASK_STACK_SENSORS];

// raw driver reading -> wire/telemetry form (the one place the obc and fsw types meet)
fsw::imu_data_t to_imu_data(const icm20948_sample_t& s) {
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

fsw::power_data_t to_power_data(const ina228_sample_t& s) {
    fsw::power_data_t d{};
    d.t_ms = s.t_ms;
    d.bus_mv = s.bus_mv;
    d.current_ma = s.current_ma;
    d.power_mw = s.power_mw;
    d.flags = static_cast<uint8_t>(s.valid ? fsw::kPowerFlagValid : 0U);
    return d;
}

fsw::temp_data_t to_temp_data(const tmp117_sample_t& s) {
    fsw::temp_data_t d{};
    d.t_ms = s.t_ms;
    d.temp_mc = s.temp_mc;
    d.flags = static_cast<uint8_t>(s.valid ? fsw::kTempFlagValid : 0U);
    return d;
}

void sensor_task(void*) {
    TickType_t next = xTaskGetTickCount();

    for (;;) {
        sensor_set_t set{};

        // a device that failed init still gets published, as its default (invalid) sample - the
        // fsw's sensor monitor is what turns that into a dropout fault, and it can only do that
        // for a sample it was handed (REQ-SNS-002)
        set.imu = s_imu_ok ? to_imu_data(icm20948_read()) : fsw::imu_data_t{};
        set.power = s_power_ok ? to_power_data(ina228_read()) : fsw::power_data_t{};
        set.temp = s_temp_ok ? to_temp_data(tmp117_read()) : fsw::temp_data_t{};

        xQueueOverwrite(s_queue, &set);

        task_health_checkin(TASK_ID_SENSORS);
        xTaskDelayUntil(&next, pdMS_TO_TICKS(kSamplePeriodMs));
    }
}

}  // namespace

void sensor_task_create(bool imu_ok, bool power_ok, bool temp_ok) {
    s_imu_ok = imu_ok;
    s_power_ok = power_ok;
    s_temp_ok = temp_ok;

    s_queue = xQueueCreateStatic(1, sizeof(sensor_set_t), s_queue_storage, &s_queue_ctrl);

    const TaskHandle_t h = xTaskCreateStatic(sensor_task, "sensors", TASK_STACK_SENSORS, nullptr,
                                             TASK_PRIO_SENSORS, s_stack, &s_tcb);
    task_health_register(TASK_ID_SENSORS, h,
                         500U);  // five missed samples, same as the control task
}

bool sensor_task_take(sensor_set_t* out) { return xQueueReceive(s_queue, out, 0) == pdTRUE; }
