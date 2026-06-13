/**
 * @file   imu.c
 * @brief  imu driver - accessing accelerometer, gyroscope, and magnetometer on ICM-20948
 */

#include "drivers/imu.h"

#include <stddef.h>

#include "drivers/spi.h"
#include "drivers/systick.h"

#define SPI_HZ   7000000U  // 7 mhz max for ICM-20948 (will go to 4mhz as next lowest from 8mhz)
#define WHO_AM_I 0xEAU     // confirm communication

// user bank 0
#define USER_CTRL            0x03U  // select communication protocol
#define PWR_MGMT_1           0x06U  // raise from sleep, enable clock
#define REG_BANK_SEL         0x7FU  // swapping user bank registers
#define ACCEL_XOUT_H         0x2DU  // start point for accel and gyro readsa
#define EXT_SLV_SENS_DATA_00 0x3BU  // where internal I2C engine copies mag data

// user bank 3
#define I2C_MST_CTRL  0x01U  // configure internal I2C clock
#define I2C_SLV0_ADDR 0x03U  // configure internal I2C slave 0 address
#define I2C_SLV0_REG  0x04U  // configure internal I2C slave 0 register
#define I2C_SLV0_CTRL 0x05U  // configure internal I2C slave 0 read
#define I2C_SLV0_DO   0x06U  // read internal I2C slave 0 data out

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

    imu_write_reg(PWR_MGMT_1, 0x80U);  // reset device
    delay_ms(10U);

    imu_write_reg(USER_CTRL, 0x10U);   // disable primary I2C slave
    imu_write_reg(PWR_MGMT_1, 0x01U);  // sleep off, auto clock
    delay_ms(10U);

    imu_write_reg(USER_CTRL, 0x30U);      // enable I2C master I/F (keep I2C_IF_DIS)
    imu_write_reg(REG_BANK_SEL, 0x30U);   // swap to user bank 3
    imu_write_reg(I2C_MST_CTRL, 0x07U);   // set clock to recommended for 400kHz max
    imu_write_reg(I2C_SLV0_ADDR, 0x0CU);  // msb 0 for write, 0x0C = mag slave addr
    imu_write_reg(I2C_SLV0_REG, 0x31U);   // mag CNTL2 register
    imu_write_reg(I2C_SLV0_DO, 0x08U);    // CNTL2 MODE4 (100Hz continuous)
    imu_write_reg(I2C_SLV0_CTRL, 0x81U);  // enable read/write and set len to 1 byte
    delay_ms(10U);

    imu_write_reg(I2C_SLV0_ADDR, 0x8CU);  // msb 1 for read, 0x0C = mag slave addr
    imu_write_reg(I2C_SLV0_REG, 0x10U);   // start at ST1 addr
    imu_write_reg(I2C_SLV0_CTRL, 0x89U);  // EN (0x80) | Length 9 bytes (0x09)
    imu_write_reg(REG_BANK_SEL, 0x00U);   // return to bank 0 for data reading

    return true;
}

void imu_read(imu_sample_t* out) {
    struct __attribute__((packed)) {
        uint8_t accel[6];     // 0x2D-0x32 icm-20948 accel x/y/z, big endian
        uint8_t gyro[6];      // 0x33-0x38 icm-20948 gyro x/y/z, big endian
        uint8_t temp[2];      // 0x39-0x3A icm-20948 DIE temperature
        uint8_t mag_st1;      // ext+0     ak09916 status 1 (drdy bit)
        uint8_t mag_data[6];  // ext+1..6  mag x/y/z, little endian
        uint8_t mag_tmps;     // ext+7     ak09916 "TMPS", dummy register
        uint8_t mag_st2;      // ext+8     ak09916 status 2 (overflow)
    } raw;

    spi_select(spi_imu);
    (void)spi_transfer(spi_imu, ACCEL_XOUT_H | 0x80U);  // start burst, address auto-increments
    for (size_t i = 0U; i < sizeof(raw); i++) {
        ((uint8_t*)&raw)[i] = spi_transfer(spi_imu, 0xFFU);
    }
    spi_deselect(spi_imu);

    // parse accel and gyro (big Endian)
    for (size_t i = 0U; i < 3U; i++) {
        out->accel[i] = (int16_t)((uint16_t)(raw.accel[2U * i] << 8U) | raw.accel[2U * i + 1U]);
        out->gyro[i] = (int16_t)((uint16_t)(raw.gyro[2U * i] << 8U) | raw.gyro[2U * i + 1U]);
    }

    // parse magnetometer (little endian)
    out->mag[0] = (int16_t)((uint16_t)(raw.mag_data[1] << 8U) | raw.mag_data[0]);
    out->mag[1] = (int16_t)((uint16_t)(raw.mag_data[3] << 8U) | raw.mag_data[2]);
    out->mag[2] = (int16_t)((uint16_t)(raw.mag_data[5] << 8U) | raw.mag_data[4]);

    out->t_ms = millis();
    out->valid = true;
}
