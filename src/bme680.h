#ifndef BME680_H
#define BME680_H

#include <stdint.h>

#define BME680_I2C_ADDR 0x76

struct bme680_data {
    float temperature;
    float humidity;
    float pressure;
    float gas_resistance;
};

int bme680_init(int i2c_fd);
int bme680_read_data(int i2c_fd, struct bme680_data *data);

#endif // BME680_H
