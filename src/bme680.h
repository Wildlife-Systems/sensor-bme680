#ifndef BME680_H
#define BME680_H

#include <stdint.h>

#define BME680_I2C_ADDR 0x76


// Structure to hold calibration data
struct bme680_calib_data {
    // Temperature
    uint16_t par_t1;
    int16_t par_t2;
    int8_t par_t3;
    // Pressure
    uint16_t par_p1;
    int16_t par_p2;
    int8_t par_p3;
    int16_t par_p4;
    int16_t par_p5;
    int8_t par_p6;
    int8_t par_p7;
    int16_t par_p8;
    int16_t par_p9;
    uint8_t par_p10;
    // Humidity
    uint16_t par_h1;
    uint16_t par_h2;
    int8_t par_h3;
    int8_t par_h4;
    int8_t par_h5;
    uint8_t par_h6;
    int8_t par_h7;
    // Gas
    int8_t par_gh1;
    int16_t par_gh2;
    int8_t par_gh3;
    uint8_t res_heat_range;
    int8_t res_heat_val;
    int8_t range_sw_err;
    // t_fine for compensation
    int32_t t_fine;
};

struct bme680_data {
    float temperature;
    float humidity;
    float pressure;
    float gas_resistance;
};

int bme680_init(int i2c_fd, struct bme680_calib_data *calib);
int bme680_read_data(int i2c_fd, struct bme680_calib_data *calib, struct bme680_data *data);

#endif // BME680_H
