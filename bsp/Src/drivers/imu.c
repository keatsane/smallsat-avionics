/**
 * @file   imu.c
 * @brief  imu driver - accessing accelerometer / gyroscope on ICM-20948
 */

#include "drivers/imu.h"

#include <stddef.h>

#include "drivers/spi.h"
#include "drivers/systick.h"

#define SPI_HZ   7000000U  // 7 mhz max for ICM-20948 (will go to 4mhz as next lowest from 8mhz)
#define WHO_AM_I 0xEAU     // value for ICM-20948 is 0xEA (confirm communication)

#define USER_CTRL  0x03U  // addr for USER_CTRL register (select communication protocol)
#define PWR_MGMT_1 0x06U  // addr for PWR_MGMT_1 register (raise from sleep, enable clock)

#define ACCEL_XOUT_H \
    0x2DU  // addr for ACCEL_XOUT_H out register (high byte of accelerometer X-axis data)
#define ACCEL_XOUT_L \
    0x2EU  // addr for ACCEL_XOUT_L out register (low byte of accelerometer X-axis data)
#define ACCEL_YOUT_H \
    0x2FU  // addr for ACCEL_YOUT_H out register (high byte of accelerometer Y-axis data)
#define ACCEL_YOUT_L \
    0x30U  // addr for ACCEL_YOUT_L out register (low byte of accelerometer Y-axis data)
#define ACCEL_ZOUT_H \
    0x31U  // addr for ACCEL_ZOUT_H out register (high byte of accelerometer Z-axis data)
#define ACCEL_ZOUT_L \
    0x32U  // addr for ACCEL_ZOUT_L out register (low byte of accelerometer Z-axis data)

#define GYRO_XOUT_H 0x33U  // addr for GYRO_XOUT_H out register (high byte of gyroscope X-axis data)
#define GYRO_XOUT_L 0x34U  // addr for GYRO_XOUT_L out register (low byte of gyroscope X-axis data)
#define GYRO_YOUT_H 0x35U  // addr for GYRO_YOUT_H out register (high byte of gyroscope Y-axis data)
#define GYRO_YOUT_L 0x36U  // addr for GYRO_YOUT_L out register (low byte of gyroscope Y-axis data)
#define GYRO_ZOUT_H 0x37U  // addr for GYRO_ZOUT_H out register (high byte of gyroscope Z-axis data)
#define GYRO_ZOUT_L 0x38U  // addr for GYRO_ZOUT_L out register (low byte of gyroscope Z-axis data)

#define REG_BANK_SEL 0x7FU  // addr for REG_BANK_SEL register (used for stm reset robustness)

static uint8_t imu_read_reg(uint8_t reg) {
    spi_select(spi_imu);
    (void)spi_transfer(spi_imu, reg | 0x80U);  // bit 7 set = read
    uint8_t v = spi_transfer(spi_imu, 0xFFU);
    spi_deselect(spi_imu);
    return v;
}

static void imu_write_reg(uint8_t reg, uint8_t val) {
    spi_select(spi_imu);
    (void)spi_transfer(spi_imu, reg);  // bit 7 clear = write
    (void)spi_transfer(spi_imu, val);
    spi_deselect(spi_imu);
}

bool imu_init(void) {
    spi_imu_init(SPI_HZ);

    imu_write_reg(REG_BANK_SEL, 0x00U);  // bank 0 - chip may have rebooted elsewhere
    if (imu_read_reg(0x00U) != WHO_AM_I) {
        return false;
    }

    imu_write_reg(PWR_MGMT_1, 0x80U);  // device_reset - known state regardless of history

    delay_ms(10U);

    imu_write_reg(USER_CTRL, 0x10U);   // i2c_if_dis - spi only
    imu_write_reg(PWR_MGMT_1, 0x01U);  // sleep off, clksel auto

    delay_ms(20U);
    return true;
}

void imu_read(imu_sample_t* out) {
    uint8_t bytes[12];  // number of bytes (6 each)
    spi_select(spi_imu);

    (void)spi_transfer(spi_imu, ACCEL_XOUT_H | 0x80U);  // burst start, auto-increments
    for (size_t i = 0U; i < sizeof bytes; i++) {
        bytes[i] = spi_transfer(spi_imu, 0xFFU);
    }

    spi_deselect(spi_imu);

    for (size_t i = 0U; i < 3U; i++) {
        out->accel[i] = (int16_t)((uint16_t)(bytes[2U * i] << 8U) | bytes[2U * i + 1U]);
        out->gyro[i] = (int16_t)((uint16_t)(bytes[6U + 2U * i] << 8U) | bytes[7U + 2U * i]);
    }

    out->t_ms = millis();
    out->valid = true;
}
