#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <time.h>
#include "bme680.h"
#include <ws_utils.h>

// Config path and default values
#define CONFIG_PATH "/etc/ws/sensors/bme680.json"
#define DEFAULT_I2C_ADDR BME680_I2C_ADDR
#define DEFAULT_I2C_DEV "/dev/i2c-1"

typedef struct {
    int internal;
    char *sensor_id;
    char *sensor_name;
} sensor_config_t;

typedef struct {
    float temperature;
    float humidity;
    float pressure;
    float gas_resistance;
    int valid;
    char error_msg[128];
} sensor_reading_t;

// Count sensor objects in JSON buffer
static int count_sensors_in_json(const char *buffer) {
    int count = 0;
    const char *ptr = buffer;
    while ((ptr = strchr(ptr, '{')) != NULL) {
        count++;
        ptr++;
    }
    return count;
}

// Parse a simple JSON config file - returns dynamically allocated array
static sensor_config_t *load_config(const char *path, int *count) {
    FILE *fp;
    char *buffer = NULL;
    char *ptr;
    int sensor_idx = 0;
    sensor_config_t *configs = NULL;
    int sensor_count;
    long file_size;
    *count = 0;
    fp = fopen(path, "r");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (file_size <= 0) { fclose(fp); return NULL; }
    buffer = malloc(file_size + 1);
    if (!buffer) { fclose(fp); return NULL; }
    size_t bytes_read = fread(buffer, 1, file_size, fp);
    buffer[bytes_read] = '\0';
    fclose(fp);
    sensor_count = count_sensors_in_json(buffer);
    if (sensor_count == 0) { free(buffer); return NULL; }
    configs = malloc(sensor_count * sizeof(sensor_config_t));
    if (!configs) { free(buffer); return NULL; }
    ptr = buffer;
    while ((ptr = strchr(ptr, '{')) != NULL && sensor_idx < sensor_count) {
        char *end = strchr(ptr, '}');
        if (!end) break;
        configs[sensor_idx].internal = 0;
        configs[sensor_idx].sensor_id = NULL;
        configs[sensor_idx].sensor_name = NULL;
        char *internal_ptr = strstr(ptr, "\"internal\"");
        if (internal_ptr && internal_ptr < end) {
            internal_ptr = strchr(internal_ptr, ':');
            if (internal_ptr) {
                while (*internal_ptr == ':' || *internal_ptr == ' ') internal_ptr++;
                configs[sensor_idx].internal = (strncmp(internal_ptr, "true", 4) == 0);
            }
        }
        char *id_ptr = strstr(ptr, "\"sensor_id\"");
        if (id_ptr && id_ptr < end) {
            id_ptr = strchr(id_ptr, ':');
            if (id_ptr) {
                char *quote_start = strchr(id_ptr, '"');
                if (quote_start && quote_start < end) {
                    quote_start++;
                    char *quote_end = strchr(quote_start, '"');
                    if (quote_end && quote_end < end) {
                        size_t id_len = quote_end - quote_start;
                        configs[sensor_idx].sensor_id = strndup(quote_start, id_len);
                    }
                }
            }
        }
        char *name_ptr = strstr(ptr, "\"sensor_name\"");
        if (name_ptr && name_ptr < end) {
            name_ptr = strchr(name_ptr, ':');
            if (name_ptr) {
                char *quote_start = strchr(name_ptr, '"');
                if (quote_start && quote_start < end) {
                    quote_start++;
                    char *quote_end = strchr(quote_start, '"');
                    if (quote_end && quote_end < end) {
                        size_t name_len = quote_end - quote_start;
                        configs[sensor_idx].sensor_name = strndup(quote_start, name_len);
                    }
                }
            }
        }
        sensor_idx++;
        ptr = end + 1;
    }
    free(buffer);
    *count = sensor_idx;
    return configs;
}

static void free_config(sensor_config_t *configs, int count) {
    if (configs) {
        for (int i = 0; i < count; i++) {
            free(configs[i].sensor_id);
            free(configs[i].sensor_name);
        }
        free(configs);
    }
}

static char *get_serial_number(const char *suffix) {
    char *raw_serial = ws_get_serial_number();
    if (!raw_serial) return NULL;
    size_t len = strlen(raw_serial) + strlen(suffix) + 2;
    char *result = malloc(len);
    if (!result) { free(raw_serial); return NULL; }
    snprintf(result, len, "%s_%s", raw_serial, suffix);
    free(raw_serial);
    return result;
}

static void build_sensor_json(char *output, size_t output_len,
    const char *sensor, const char *measures, const char *unit,
    float value, int internal, const char *sensor_id,
    const char *sensor_name, const char *error_msg, time_t timestamp) {
    const char *prototype = ws_get_prototype_cached();
    char timestamp_str[32];
    if (!prototype || !*prototype) {
        output[0] = '\0';
        return;
    }
    strncpy(output, prototype, output_len - 1);
    output[output_len - 1] = '\0';
    ws_json_replace_null_string(output, "sensor", sensor);
    ws_json_replace_null_string(output, "measures", measures);
    ws_json_replace_null_string(output, "unit", unit);
    ws_json_replace_null_string(output, "sensor_id", sensor_id);
    if (sensor_name && sensor_name[0] != '\0') {
        ws_json_replace_null_string(output, "sensor_name", sensor_name);
    }
    ws_json_replace_null_bool(output, "internal", internal);
    snprintf(timestamp_str, sizeof(timestamp_str), "%ld", (long)timestamp);
    ws_json_replace_null_string(output, "timestamp", timestamp_str);
    if (error_msg) {
        char escaped_error[256];
        ws_json_escape_string(error_msg, escaped_error, sizeof(escaped_error));
        ws_json_replace_null_bool(output, "value", 0);
        ws_json_replace_null_string(output, "error", escaped_error);
    } else {
        ws_json_replace_null_number(output, "value", (double)value);
        // Leave error as null - don't replace it
    }
}

static void output_json(sensor_config_t *configs, int count, const char *filter, int location_filter) {
    size_t output_size = 4096;
    char *output = malloc(output_size);
    if (!output) { fprintf(stderr, "Memory allocation failed\n"); return; }
    strcpy(output, "[");
    int first = 1;
    for (int i = 0; i < count; i++) {
        sensor_reading_t reading = {0};
        char *sensor_id = configs[i].sensor_id ? strdup(configs[i].sensor_id) : get_serial_number("bme680");
        const char *error_msg = NULL;
        time_t read_timestamp = time(NULL);
        if (location_filter == 1 && !configs[i].internal) { free(sensor_id); continue; }
        if (location_filter == 2 && configs[i].internal) { free(sensor_id); continue; }
        // Read sensor
        int i2c_fd = open(DEFAULT_I2C_DEV, O_RDWR);
        if (i2c_fd < 0) {
            snprintf(reading.error_msg, sizeof(reading.error_msg), "Failed to open I2C device");
            error_msg = reading.error_msg;
        } else if (ioctl(i2c_fd, I2C_SLAVE, DEFAULT_I2C_ADDR) < 0) {
            snprintf(reading.error_msg, sizeof(reading.error_msg), "Failed to set I2C address");
            error_msg = reading.error_msg;
            close(i2c_fd);
        } else {
            struct bme680_calib_data calib;
            if (bme680_init(i2c_fd, &calib) != 0) {
                snprintf(reading.error_msg, sizeof(reading.error_msg), "BME680 not found or init failed");
                error_msg = reading.error_msg;
            } else if (bme680_read_data(i2c_fd, &calib, (struct bme680_data *)&reading) != 0) {
                snprintf(reading.error_msg, sizeof(reading.error_msg), "Failed to read sensor data");
                error_msg = reading.error_msg;
            } else {
                reading.valid = 1;
            }
            close(i2c_fd);
        }
        // Output JSON for each measurement
        if (!filter || strcmp(filter, "temperature") == 0 || strcmp(filter, "all") == 0) {
            char temp_json[1024];
            char *sensor_id_temp = malloc(strlen(sensor_id) + 16);
            snprintf(sensor_id_temp, strlen(sensor_id) + 16, "%s_temperature", sensor_id);
            build_sensor_json(temp_json, sizeof(temp_json),
                "bme680_temperature", "temperature", "Celsius",
                reading.temperature, configs[i].internal, sensor_id_temp,
                configs[i].sensor_name, error_msg, read_timestamp);
            size_t needed = strlen(output) + strlen(temp_json) + 3;
            if (needed > output_size) { output_size = needed * 2; char *new_output = realloc(output, output_size); if (new_output) output = new_output; }
            if (!first) strcat(output, ",");
            strcat(output, temp_json);
            first = 0;
            free(sensor_id_temp);
        }
        if (!filter || strcmp(filter, "humidity") == 0 || strcmp(filter, "all") == 0) {
            char humid_json[1024];
            char *sensor_id_humid = malloc(strlen(sensor_id) + 16);
            snprintf(sensor_id_humid, strlen(sensor_id) + 16, "%s_humidity", sensor_id);
            build_sensor_json(humid_json, sizeof(humid_json),
                "bme680_humidity", "humidity", "percentage",
                reading.humidity, configs[i].internal, sensor_id_humid,
                configs[i].sensor_name, error_msg, read_timestamp);
            size_t needed = strlen(output) + strlen(humid_json) + 3;
            if (needed > output_size) { output_size = needed * 2; char *new_output = realloc(output, output_size); if (new_output) output = new_output; }
            if (!first) strcat(output, ",");
            strcat(output, humid_json);
            first = 0;
            free(sensor_id_humid);
        }
        if (!filter || strcmp(filter, "pressure") == 0 || strcmp(filter, "all") == 0) {
            char press_json[1024];
            char *sensor_id_press = malloc(strlen(sensor_id) + 16);
            snprintf(sensor_id_press, strlen(sensor_id) + 16, "%s_pressure", sensor_id);
            build_sensor_json(press_json, sizeof(press_json),
                "bme680_pressure", "pressure", "hPa",
                reading.pressure / 100.0, configs[i].internal, sensor_id_press,
                configs[i].sensor_name, error_msg, read_timestamp);
            size_t needed = strlen(output) + strlen(press_json) + 3;
            if (needed > output_size) { output_size = needed * 2; char *new_output = realloc(output, output_size); if (new_output) output = new_output; }
            if (!first) strcat(output, ",");
            strcat(output, press_json);
            first = 0;
            free(sensor_id_press);
        }
        if (!filter || strcmp(filter, "gas") == 0 || strcmp(filter, "all") == 0) {
            char gas_json[1024];
            char *sensor_id_gas = malloc(strlen(sensor_id) + 16);
            snprintf(sensor_id_gas, strlen(sensor_id) + 16, "%s_gas_resistance", sensor_id);
            build_sensor_json(gas_json, sizeof(gas_json),
                "bme680_gas", "resistance", "Ohms",
                reading.gas_resistance, configs[i].internal, sensor_id_gas,
                configs[i].sensor_name, error_msg, read_timestamp);
            size_t needed = strlen(output) + strlen(gas_json) + 3;
            if (needed > output_size) { output_size = needed * 2; char *new_output = realloc(output, output_size); if (new_output) output = new_output; }
            if (!first) strcat(output, ",");
            strcat(output, gas_json);
            first = 0;
            free(sensor_id_gas);
        }
        free(sensor_id);
    }
    strcat(output, "]");
    printf("%s\n", output);
    free(output);
}

int main(int argc, char *argv[]) {
    sensor_config_t *configs = NULL;
    sensor_config_t default_config;
    int config_count = 0;
    const char *filter = NULL;
    int location_filter = 0; // 0=all, 1=internal, 2=external

    if (argc >= 2) {
        if (strcmp(argv[1], "identify") == 0) {
            ws_cmd_identify();
        } else if (strcmp(argv[1], "list") == 0) {
            static const char *measurements[] = {"temperature", "humidity", "pressure", "gas", NULL};
            ws_cmd_list_multiple(measurements);
        } else if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "version") == 0) {
            ws_print_version("sensor-bme680", "1.0.0");
            return 0;
        } else if (strcmp(argv[1], "temperature") == 0 || strcmp(argv[1], "humidity") == 0 || strcmp(argv[1], "pressure") == 0 || strcmp(argv[1], "gas") == 0) {
            filter = argv[1];
        } else if (strcmp(argv[1], "internal") == 0) {
            location_filter = 1;
        } else if (strcmp(argv[1], "external") == 0) {
            location_filter = 2;
        } else if (strcmp(argv[1], "all") != 0) {
            fprintf(stderr, "Unknown command: %s\n", argv[1]);
            fprintf(stderr, "Usage: sensor-bme680 [--version|identify|list|temperature|humidity|pressure|gas|internal|external|all]\n");
            return 1;
        }
    }

    configs = load_config(CONFIG_PATH, &config_count);
    if (configs == NULL || config_count == 0) {
        char *serial = get_serial_number("bme680");
        // default_config.i2c_addr = DEFAULT_I2C_ADDR;
        default_config.internal = 0;
        default_config.sensor_id = serial;
        default_config.sensor_name = NULL;
        configs = &default_config;
        config_count = 1;
    }

    output_json(configs, config_count, filter, location_filter);

    if (configs == &default_config) {
        free(default_config.sensor_id);
        free(default_config.sensor_name);
    } else {
        free_config(configs, config_count);
    }
    return 0;
}
