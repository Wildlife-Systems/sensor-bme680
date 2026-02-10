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
    i2c_read_byte(i2c_fd, 0xE3, &h1_msb); i2c_read_byte(i2c_fd, 0xE2, &h1_lsb);
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

// Helper: calculate heater resistance register value (Bosch BME68x API reference)
static uint8_t calc_res_heat(uint16_t target_temp, int16_t amb_temp, struct bme680_calib_data *calib) {
    if (target_temp > 400) target_temp = 400;
    int32_t var1 = (((int32_t)amb_temp * calib->par_gh3) / 1000) * 256;
    int32_t var2 = (calib->par_gh1 + 784) *
                   (((((calib->par_gh2 + 154009) * target_temp * 5) / 100) + 3276800) / 10);
    int32_t var3 = var1 + (var2 / 2);
    int32_t var4 = (var3 / (calib->res_heat_range + 4));
    int32_t var5 = (131 * calib->res_heat_val) + 65536;
    int32_t heatr_res_x100 = (int32_t)(((var4 / var5) - 250) * 34);
    return (uint8_t)((heatr_res_x100 + 50) / 100);
}

// Helper: encode gas wait duration in ms to register value (Bosch BME68x API reference)
static uint8_t calc_gas_wait(uint16_t dur) {
    uint8_t factor = 0;
    if (dur >= 0xfc0) return 0xff;
    while (dur > 0x3F) {
        dur /= 4;
        factor++;
    }
    return (uint8_t)(dur + (factor * 64));
}

int bme680_read_data(int i2c_fd, struct bme680_calib_data *calib, struct bme680_data *data) {
    // 1. Configure gas heater (320°C target, 150ms duration, 25°C ambient)
    uint8_t res_heat = calc_res_heat(320, 25, calib);
    uint8_t gas_wait = calc_gas_wait(150);
    uint8_t res_heat_0[2] = {0x5A, res_heat};
    uint8_t gas_wait_0[2] = {0x64, gas_wait};
    if (write(i2c_fd, res_heat_0, 2) != 2) return -1;
    if (write(i2c_fd, gas_wait_0, 2) != 2) return -1;

    // 2. Enable gas measurement (run_gas=1, nb_conv=0)
    uint8_t ctrl_gas_1[2] = {0x71, 0x10};
    if (write(i2c_fd, ctrl_gas_1, 2) != 2) return -1;

    // 3. Set oversampling and trigger forced mode
    uint8_t ctrl_hum[2] = {0x72, 0x01};
    uint8_t ctrl_meas[2] = {0x74, 0x25};
    if (write(i2c_fd, ctrl_hum, 2) != 2) return -1;
    if (write(i2c_fd, ctrl_meas, 2) != 2) return -1;

    // 4. Poll for measurement completion (new_data bit in status register)
    uint8_t status = 0;
    int retries = 50; // up to 500ms
    do {
        usleep(10000);
        uint8_t sreg = 0x1D;
        if (write(i2c_fd, &sreg, 1) != 1) return -1;
        if (read(i2c_fd, &status, 1) != 1) return -1;
    } while (!(status & 0x80) && --retries > 0);
    if (retries == 0) return -3; // measurement timeout

    // 5. Read T/P/H raw data (0x1F-0x26)
    uint8_t reg = 0x1F;
    uint8_t buf[8];
    if (write(i2c_fd, &reg, 1) != 1) return -1;
    if (read(i2c_fd, buf, 8) != 8) return -1;
    int32_t adc_press = ((int32_t)buf[0] << 12) | ((int32_t)buf[1] << 4) | ((int32_t)buf[2] >> 4);
    int32_t adc_temp  = ((int32_t)buf[3] << 12) | ((int32_t)buf[4] << 4) | ((int32_t)buf[5] >> 4);
    int32_t adc_hum   = ((int32_t)buf[6] << 8) | (int32_t)buf[7];

    // 6. Read gas raw data (0x2A-0x2B)
    uint8_t gas_buf[2];
    reg = 0x2A;
    if (write(i2c_fd, &reg, 1) != 1) return -1;
    if (read(i2c_fd, gas_buf, 2) != 2) return -1;
    uint16_t adc_gas_res = ((uint16_t)gas_buf[0] << 2) | (gas_buf[1] >> 6);
    uint8_t gas_valid = (gas_buf[1] >> 5) & 0x01;
    uint8_t gas_range = gas_buf[1] & 0x0F;



    // Temperature compensation (Bosch datasheet 9.2.3.3)
    int64_t var1, var2;
    var1 = (((int32_t)adc_temp >> 3) - ((int32_t)calib->par_t1 << 1));
    var1 = (var1 * ((int32_t)calib->par_t2)) >> 11;
    var2 = (((((int32_t)adc_temp >> 4) - ((int32_t)calib->par_t1)) * (((int32_t)adc_temp >> 4) - ((int32_t)calib->par_t1))) >> 12);
    var2 = (var2 * (((int32_t)calib->par_t3) << 4)) >> 14;
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

    // Humidity compensation (Bosch BME68x API reference)
    int32_t temp_scaled = ((calib->t_fine * 5) + 128) >> 8;
    int32_t var_h1 = (adc_hum - ((int32_t)calib->par_h1 << 4)) - (((temp_scaled * (int32_t)calib->par_h3) / 100) >> 1);
    int32_t var_h2 = ((int32_t)calib->par_h2 * (((temp_scaled * (int32_t)calib->par_h4) / 100) + (((temp_scaled * ((temp_scaled * (int32_t)calib->par_h5) / 100)) >> 6) / 100) + (int32_t)(1 << 14))) >> 10;
    int32_t var_h3 = var_h1 * var_h2;
    int32_t var_h4 = (((int32_t)calib->par_h6 << 7) + ((temp_scaled * (int32_t)calib->par_h7) / 100)) >> 4;
    int32_t var_h5 = ((var_h3 >> 14) * (var_h3 >> 14)) >> 10;
    int32_t var_h6 = (var_h4 * var_h5) >> 1;
    int32_t calc_hum = (((var_h3 + var_h6) >> 10) * 1000) >> 12;
    if (calc_hum > 100000) calc_hum = 100000;
    else if (calc_hum < 0) calc_hum = 0;
    data->humidity = (float)calc_hum / 1000.0f;

    // Gas resistance compensation (Bosch BME68x API reference)
    if (gas_valid) {
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
        data->gas_resistance = (float)((gas_var3 + ((int64_t)gas_var2 >> 1)) / (int64_t)gas_var2);
    } else {
        data->gas_resistance = 0.0f;
    }
    return 0;
}
