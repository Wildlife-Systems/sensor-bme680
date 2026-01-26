#define _GNU_SOURCE
#include "bme680.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define BME680_REG_ID 0xD0


// Helper to read 1 byte
static int i2c_read_byte(int fd, uint8_t reg, uint8_t *val) {
    if (write(fd, &reg, 1) != 1) return -1;
    if (read(fd, val, 1) != 1) return -1;
    return 0;
}

// Helper to read 2 bytes (little endian)
static int i2c_read_word(int fd, uint8_t reg, uint16_t *val) {
    uint8_t buf[2];
    if (write(fd, &reg, 1) != 1) return -1;
    if (read(fd, buf, 2) != 2) return -1;
    *val = buf[0] | (buf[1] << 8);
    return 0;
}

int bme680_init(int i2c_fd, struct bme680_calib_data *calib) {
    // 1. Check chip ID
    uint8_t reg = BME680_REG_ID;
    uint8_t id = 0;
    if (write(i2c_fd, &reg, 1) != 1) return -1;
    if (read(i2c_fd, &id, 1) != 1) return -1;
    if (id != 0x61) return -2; // BME680 chip ID

    // 2. Read all calibration data (see BME680 datasheet Table 18)
    // Temperature
    i2c_read_word(i2c_fd, 0xE9, &calib->par_t1);
    uint16_t t2; i2c_read_word(i2c_fd, 0x8A, &t2); calib->par_t2 = (int16_t)t2;
    uint8_t t3; i2c_read_byte(i2c_fd, 0x8C, &t3); calib->par_t3 = (int8_t)t3;
    // Pressure
    i2c_read_word(i2c_fd, 0x8E, &calib->par_p1);
    uint16_t p2; i2c_read_word(i2c_fd, 0x90, &p2); calib->par_p2 = (int16_t)p2;
    uint8_t p3; i2c_read_byte(i2c_fd, 0x92, &p3); calib->par_p3 = (int8_t)p3;
    uint16_t p4; i2c_read_word(i2c_fd, 0x94, &p4); calib->par_p4 = (int16_t)p4;
    uint16_t p5; i2c_read_word(i2c_fd, 0x96, &p5); calib->par_p5 = (int16_t)p5;
    uint8_t p6; i2c_read_byte(i2c_fd, 0x99, &p6); calib->par_p6 = (int8_t)p6;
    uint8_t p7; i2c_read_byte(i2c_fd, 0x98, &p7); calib->par_p7 = (int8_t)p7;
    uint16_t p8; i2c_read_word(i2c_fd, 0x9C, &p8); calib->par_p8 = (int16_t)p8;
    uint16_t p9; i2c_read_word(i2c_fd, 0x9E, &p9); calib->par_p9 = (int16_t)p9;
    i2c_read_byte(i2c_fd, 0xA0, &calib->par_p10);
    // Humidity
    uint8_t h1_lsb, h1_msb, h2_lsb, h2_msb;
    i2c_read_byte(i2c_fd, 0xE2, &h1_msb); i2c_read_byte(i2c_fd, 0xE3, &h1_lsb);
    i2c_read_byte(i2c_fd, 0xE1, &h2_msb); i2c_read_byte(i2c_fd, 0xE2, &h2_lsb);
    calib->par_h1 = (h1_msb << 4) | (h1_lsb & 0x0F);
    calib->par_h2 = (h2_msb << 4) | (h2_lsb >> 4);
    uint8_t h3; i2c_read_byte(i2c_fd, 0xE4, &h3); calib->par_h3 = (int8_t)h3;
    uint8_t h4; i2c_read_byte(i2c_fd, 0xE5, &h4); calib->par_h4 = (int8_t)h4;
    uint8_t h5; i2c_read_byte(i2c_fd, 0xE6, &h5); calib->par_h5 = (int8_t)h5;
    uint8_t h6; i2c_read_byte(i2c_fd, 0xE7, &h6); calib->par_h6 = h6;
    uint8_t h7; i2c_read_byte(i2c_fd, 0xE8, &h7); calib->par_h7 = (int8_t)h7;
    // Gas
    uint8_t gh1; i2c_read_byte(i2c_fd, 0xED, &gh1); calib->par_gh1 = (int8_t)gh1;
    uint16_t gh2; i2c_read_word(i2c_fd, 0xEB, &gh2); calib->par_gh2 = (int16_t)gh2;
    uint8_t gh3; i2c_read_byte(i2c_fd, 0xEE, &gh3); calib->par_gh3 = (int8_t)gh3;
    uint8_t res_heat_range, res_heat_val, range_sw_err;
    i2c_read_byte(i2c_fd, 0x02, &res_heat_range); calib->res_heat_range = (res_heat_range & 0x30) >> 4;
    i2c_read_byte(i2c_fd, 0x00, &res_heat_val); calib->res_heat_val = (int8_t)res_heat_val;
    i2c_read_byte(i2c_fd, 0x04, &range_sw_err); calib->range_sw_err = (int8_t)(range_sw_err & 0xF0) >> 4;
    return 0;
}

int bme680_read_data(int i2c_fd, struct bme680_calib_data *calib, struct bme680_data *data) {
    // 1. Set sensor to forced mode and trigger measurement
    // Set oversampling for temp, press, hum (1x oversampling for all)
    uint8_t ctrl_hum[2] = {0x72, 0x01}; // ctrl_hum: 0x72, value: 0x01 (osrs_h[2:0]=001)
    uint8_t ctrl_meas[2] = {0x74, 0x25}; // ctrl_meas: 0x74, value: 0x25 (osrs_t=001, osrs_p=001, mode=01)
    if (write(i2c_fd, ctrl_hum, 2) != 2) return -1;
    if (write(i2c_fd, ctrl_meas, 2) != 2) return -1;
    // Wait for measurement to complete (max 10ms for 1x oversampling)
    usleep(10000);

    // 2. Read all raw data in one burst (0x1F to 0x26)
    uint8_t reg = 0x1F;
    uint8_t buf[8];
    if (write(i2c_fd, &reg, 1) != 1) return -1;
    if (read(i2c_fd, buf, 8) != 8) return -1;
    // buf[0]: press_msb, buf[1]: press_lsb, buf[2]: press_xlsb
    // buf[3]: temp_msb, buf[4]: temp_lsb, buf[5]: temp_xlsb
    // buf[6]: hum_msb, buf[7]: hum_lsb
    int32_t adc_press = ((int32_t)buf[0] << 12) | ((int32_t)buf[1] << 4) | ((int32_t)buf[2] >> 4);
    int32_t adc_temp  = ((int32_t)buf[3] << 12) | ((int32_t)buf[4] << 4) | ((int32_t)buf[5] >> 4);
    int32_t adc_hum   = ((int32_t)buf[6] << 8) | (int32_t)buf[7];



    // Temperature compensation (Bosch datasheet 9.2.3.3)
    int64_t var1, var2;
    var1 = (((int32_t)adc_temp >> 3) - ((int32_t)calib->par_t1 << 1));
    var1 = (var1 * ((int32_t)calib->par_t2)) >> 11;
    var2 = (((((int32_t)adc_temp >> 4) - ((int32_t)calib->par_t1)) * (((int32_t)adc_temp >> 4) - ((int32_t)calib->par_t1))) >> 12);
    var2 = (var2 * ((int32_t)calib->par_t3)) >> 14;
    calib->t_fine = (int32_t)(var1 + var2);
    data->temperature = ((calib->t_fine * 5 + 128) >> 8) / 100.0f;

    // Pressure compensation (Bosch datasheet 9.2.3.4)
    int64_t var_p1, var_p2;
    var_p1 = ((int64_t)calib->t_fine) - 128000;
    var_p2 = var_p1 * var_p1 * (int64_t)calib->par_p6;
    var_p2 = var_p2 + ((var_p1 * (int64_t)calib->par_p5) << 17);
    var_p2 = var_p2 + (((int64_t)calib->par_p4) << 35);
    var_p1 = ((var_p1 * var_p1 * (int64_t)calib->par_p3) >> 8) + ((var_p1 * (int64_t)calib->par_p2) << 12);
    var_p1 = (((((int64_t)1) << 47) + var_p1)) * ((int64_t)calib->par_p1) >> 33;
    float pressure = 0.0f;
    if (var_p1 != 0) {
        int64_t p = 1048576 - adc_press;
        p = (((p << 31) - var_p2) * 3125) / var_p1;
        var_p1 = (((int64_t)calib->par_p9) * (p >> 13) * (p >> 13)) >> 25;
        var_p2 = (((int64_t)calib->par_p8) * p) >> 19;
        p = ((p + var_p1 + var_p2) >> 8) + (((int64_t)calib->par_p7) << 4);
        pressure = (float)p / 256.0f;
    }
    data->pressure = pressure;

    // Humidity compensation (Bosch datasheet 9.2.3.5, simplified)
    int32_t temp_scaled = ((calib->t_fine * 5) + 128) >> 8;
    int32_t var_h1 = (adc_hum - ((int32_t)calib->par_h1 << 4)) - (((temp_scaled * (int32_t)calib->par_h3) / 100) >> 1);
    int32_t var_h2 = ((int32_t)calib->par_h2 * (((temp_scaled * (int32_t)calib->par_h4) / 100) + (((temp_scaled * ((temp_scaled * (int32_t)calib->par_h5) / 100)) >> 6) / 100) + (int32_t)(1 << 14))) >> 10;
    int32_t var_h = var_h1 * var_h2;
    var_h = var_h - ((((var_h >> 14) * (var_h >> 14)) >> 10) * ((int32_t)calib->par_h7));
    var_h = var_h >> 1;
    var_h = (var_h < 0 ? 0 : var_h);
    var_h = (var_h > 419430400 ? 419430400 : var_h);
    data->humidity = (float)(var_h >> 12) / 1024.0f;

    // Gas resistance reading and compensation (Bosch/Python reference)
    // 1. Set gas sensor heater to enable gas measurement
    // Heater control: set nb conversion to 0, run gas measurement
    uint8_t ctrl_gas_1[2] = {0x71, 0x10}; // ctrl_gas_1: 0x71, value: 0x10 (run_gas=1, nb_conv=0)
    if (write(i2c_fd, ctrl_gas_1, 2) != 2) return -1;
    // Wait for gas measurement (max 250ms)
    usleep(250000);

    // Read gas resistance registers (0x2A, 0x2B)
    uint8_t gas_buf[2];
    reg = 0x2A;
    if (write(i2c_fd, &reg, 1) != 1) return -1;
    if (read(i2c_fd, gas_buf, 2) != 2) return -1;
    uint16_t adc_gas_res = ((uint16_t)gas_buf[0] << 2) | (gas_buf[1] >> 6);
    uint8_t gas_range = gas_buf[1] & 0x0F;

    // Full Bosch/Python formula for gas resistance
    static const uint32_t lookupTable1[16] = {
        2147483647, 2147483647, 2147483647, 2147483647, 2147483647, 2126008810, 2147483647, 2130303777,
        2147483647, 2147483647, 2143188679, 2136746228, 2147483647, 2126008810, 2147483647, 2147483647
    };
    static const uint32_t lookupTable2[16] = {
        4096000000, 2048000000, 1024000000, 512000000, 255744255, 127110228, 64000000, 32258064,
        16016016, 8000000, 4000000, 2000000, 1000000, 500000, 250000, 125000
    };
    int64_t gas_var1 = (int64_t)((1340 + (5 * (int64_t)calib->range_sw_err)) * (int64_t)lookupTable1[gas_range]) >> 16;
    int64_t gas_var2 = (((int64_t)((int64_t)adc_gas_res << 15) - 16777216) + gas_var1);
    int64_t gas_var3 = ((int64_t)lookupTable2[gas_range] * (int64_t)gas_var1) >> 9;
    float gas_res = (float)((gas_var3 + ((int64_t)gas_var2 >> 1)) / (int64_t)gas_var2);
    data->gas_resistance = gas_res;
    return 0;
}
