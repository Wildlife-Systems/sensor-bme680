#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include "bme680.h"

int main() {
    const char *i2c_device = "/dev/i2c-1";
    int i2c_fd = open(i2c_device, O_RDWR);
    if (i2c_fd < 0) {
        perror("Failed to open I2C device");
        return 1;
    }
    if (ioctl(i2c_fd, I2C_SLAVE, BME680_I2C_ADDR) < 0) {
        perror("Failed to set I2C address");
        close(i2c_fd);
        return 1;
    }
    if (bme680_init(i2c_fd) != 0) {
        fprintf(stderr, "BME680 not found or init failed\n");
        close(i2c_fd);
        return 1;
    }
    struct bme680_data data;
    if (bme680_read_data(i2c_fd, &data) == 0) {
        printf("Temperature: %.2f\n", data.temperature);
        printf("Humidity: %.2f\n", data.humidity);
        printf("Pressure: %.2f\n", data.pressure);
        printf("Gas Resistance: %.2f\n", data.gas_resistance);
    } else {
        fprintf(stderr, "Failed to read sensor data\n");
    }
    close(i2c_fd);
    return 0;
}
