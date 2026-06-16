/**
 * @file   icm20948.c
 * @brief  icm-20948 driver - 9-dof accel/gyro/mag over spi
 */

#include "devices/icm20948.h"

#include <stddef.h>

#include "drivers/spi.h"
#include "drivers/systick.h"

#define WHO_AM_I 0xEAU  // confirm communication

#define GYRO_CAL_SAMPLES 64U  // still-burst length for the boot gyro bias measurement

// registers
// user bank 0
#define USER_CTRL            0x03U  // select communication protocol
#define PWR_MGMT_1           0x06U  // raise from sleep, enable clock
#define REG_BANK_SEL         0x7FU  // swapping user bank registers
#define ACCEL_XOUT_H         0x2DU  // start point for accel and gyro reads
#define EXT_SLV_SENS_DATA_00 0x3BU  // where internal I2C engine copies mag data

// user bank 2
#define GYRO_SMPLRT_DIV    0x00U  // gyro output rate divider
#define GYRO_CONFIG_1      0x01U  // gyro full scale + dlpf
#define ACCEL_SMPLRT_DIV_1 0x10U  // accel rate divider [11:8]
#define ACCEL_SMPLRT_DIV_2 0x11U  // accel rate divider [7:0]
#define ACCEL_CONFIG       0x14U  // accel full scale + dlpf

// user bank 3
#define I2C_MST_ODR_CONFIG 0x00U  // external-sensor (mag) i2c-master poll rate
#define I2C_MST_CTRL       0x01U  // configure internal I2C clock
#define I2C_SLV0_ADDR      0x03U  // configure internal I2C slave 0 address
#define I2C_SLV0_REG       0x04U  // configure internal I2C slave 0 register
#define I2C_SLV0_CTRL      0x05U  // configure internal I2C slave 0 read
#define I2C_SLV0_DO        0x06U  // read internal I2C slave 0 data out

static int16_t gyro_bias[3];  // gyro zero-rate offset measured at boot, removed on every read

static uint8_t reg_read(uint8_t reg) {
    spi_select(spi_imu);
    (void)spi_transfer_byte(spi_imu, reg | 0x80U);  // bit 7 set = read
    uint8_t v = spi_transfer_byte(spi_imu, 0xFFU);
    spi_deselect(spi_imu);
    return v;
}

static void reg_write(uint8_t reg, uint8_t val) {
    spi_select(spi_imu);
    (void)spi_transfer_byte(spi_imu, reg);  // bit 7 clear = write
    (void)spi_transfer_byte(spi_imu, val);
    spi_deselect(spi_imu);
}

// saturate to int16 so subtracting the gyro bias can't wrap a railed reading
static int16_t clamp_i16(int32_t v) {
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return (int16_t)v;
}

bool icm20948_init(void) {
    spi_imu_init();

    reg_write(REG_BANK_SEL, 0x00U);  // bank 0 - chip may have rebooted elsewhere
    if (reg_read(0x00U) != WHO_AM_I) {
        return false;
    }

    reg_write(PWR_MGMT_1, 0x80U);  // reset device
    delay_ms(10U);

    reg_write(USER_CTRL, 0x10U);   // disable primary I2C slave
    reg_write(PWR_MGMT_1, 0x01U);  // sleep off, auto clock
    delay_ms(10U);

    reg_write(USER_CTRL, 0x30U);  // enable I2C master I/F (keep I2C_IF_DIS)

    // bank 2 - accel/gyro output rate, full scale, and anti-alias dlpf
    reg_write(REG_BANK_SEL, 0x20U);        // swap to user bank 2
    reg_write(GYRO_SMPLRT_DIV, 0x0BU);     // gyro odr 1125/12 = 93.75hz
    reg_write(GYRO_CONFIG_1, 0x35U);       // +-1000 dps + 5.7hz dlpf
    reg_write(ACCEL_SMPLRT_DIV_1, 0x00U);  // accel rate divider [11:8]
    reg_write(ACCEL_SMPLRT_DIV_2, 0x0BU);  // accel odr 1125/12 = 93.75hz
    reg_write(ACCEL_CONFIG, 0x31U);        // +-2g + 5.7hz dlpf

    // bank 3 - internal I2C master driving the ak09916 magnetometer
    reg_write(REG_BANK_SEL, 0x30U);   // swap to user bank 3
    reg_write(I2C_MST_CTRL, 0x07U);   // set clock to recommended for 400kHz max
    reg_write(I2C_SLV0_ADDR, 0x0CU);  // msb 0 for write, 0x0C = mag slave addr
    reg_write(I2C_SLV0_REG, 0x31U);   // mag CNTL2 register
    reg_write(I2C_SLV0_DO, 0x08U);    // CNTL2 MODE4 (100Hz continuous)
    reg_write(I2C_SLV0_CTRL, 0x81U);  // enable read/write and set len to 1 byte
    delay_ms(10U);

    reg_write(I2C_SLV0_ADDR, 0x8CU);  // msb 1 for read, 0x0C = mag slave addr
    reg_write(I2C_SLV0_REG, 0x10U);   // start at ST1 addr
    reg_write(I2C_SLV0_CTRL, 0x89U);  // EN (0x80) | Length 9 bytes (0x09)
    // the duty-cycled master polls the mag at this odr: 1100/2^4 = 68.75hz, just under the mag's
    // 100hz so drdy reads fresh every time. set last, so the CNTL2 write above ran at full speed
    reg_write(I2C_MST_ODR_CONFIG, 0x04U);

    reg_write(REG_BANK_SEL, 0x00U);  // return to bank 0 for data reading

    // gyro zero-rate calibration - average a still burst and subtract it on every read.
    // the board must be stationary at boot for this to mean anything
    int32_t bias_sum[3] = {0, 0, 0};
    for (uint16_t n = 0U; n < GYRO_CAL_SAMPLES; n++) {
        icm20948_sample_t s = icm20948_read();
        for (size_t i = 0U; i < 3U; i++) {
            bias_sum[i] += s.gyro[i];
        }
        delay_ms(10U);
    }
    for (size_t i = 0U; i < 3U; i++) {
        gyro_bias[i] = (int16_t)(bias_sum[i] / (int32_t)GYRO_CAL_SAMPLES);
    }

    return true;
}

icm20948_sample_t icm20948_read(void) {
    icm20948_sample_t sample;

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
    (void)spi_transfer_byte(spi_imu, ACCEL_XOUT_H | 0x80U);  // start burst, address auto-increments
    spi_transfer_buf(spi_imu, NULL, (uint8_t*)&raw, sizeof(raw));
    spi_deselect(spi_imu);

    // parse accel and gyro (big Endian)
    for (size_t i = 0U; i < 3U; i++) {
        sample.accel[i] = (int16_t)((uint16_t)(raw.accel[2U * i] << 8U) | raw.accel[2U * i + 1U]);
        int16_t g = (int16_t)((uint16_t)(raw.gyro[2U * i] << 8U) | raw.gyro[2U * i + 1U]);
        sample.gyro[i] = clamp_i16((int32_t)g - gyro_bias[i]);  // drop the boot zero-rate offset
    }

    // parse magnetometer (little endian)
    sample.mag[0] = (int16_t)((uint16_t)(raw.mag_data[1] << 8U) | raw.mag_data[0]);
    sample.mag[1] = (int16_t)((uint16_t)(raw.mag_data[3] << 8U) | raw.mag_data[2]);
    sample.mag[2] = (int16_t)((uint16_t)(raw.mag_data[5] << 8U) | raw.mag_data[4]);

    // accel/gyro count as not responding only if every byte read back 0xff - a floating spi bus
    // (unplugged or dead) reads all-ones, which a live sensor never does across all 12 bytes.
    // accel and gyro share one die and one burst, so they share one validity bit
    bool accel_gyro_alive = false;
    for (size_t i = 0U; i < 12U; i++) {  // accel(6) + gyro(6)
        if (((uint8_t*)&raw)[i] != 0xFFU) {
            accel_gyro_alive = true;
            break;
        }
    }

    sample.t_ms = millis();
    sample.accel_gyro_valid = accel_gyro_alive;
    // mag good only when the ak09916 reported fresh data (st1 drdy) with no overflow (st2 hofl)
    sample.mag_valid = ((raw.mag_st1 & 0x01U) != 0U) && ((raw.mag_st2 & 0x08U) == 0U);

    return sample;
}
