/*
 * sensor-bme680 - Read BME680 environmental sensor on Raspberry Pi
 * Copyright (C) 2024 Wildlife Systems
 *
 * Part of the WildlifeSystems project.
 * https://wildlife.systems
 *
 * This program reads temperature, humidity, pressure, and gas resistance
 * from Bosch BME680 sensors connected via I2C.
 *
 * License: GPL-2+
 */

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
#define DEFAULT_I2C_DEV "/dev/i2c-1"

// Try to detect BME680 at primary address (0x76), then secondary (0x77)
// Returns the detected address, or -1 if not found
static int detect_i2c_address(int i2c_fd) {
    uint8_t addresses[] = {BME680_I2C_ADDR_PRIMARY, BME680_I2C_ADDR_SECONDARY};
    for (int i = 0; i < 2; i++) {
        if (ioctl(i2c_fd, I2C_SLAVE, addresses[i]) < 0) continue;
        uint8_t reg = 0xD0; // BME680 chip ID register
        uint8_t id = 0;
        if (write(i2c_fd, &reg, 1) == 1 && read(i2c_fd, &id, 1) == 1 && id == 0x61) {
            return addresses[i];
        }
    }
    return -1;
}

typedef struct {
    int internal;
    char *sensor_id;
    char *sensor_name;
    int i2c_addr;  // 0x76, 0x77, or 0 for auto-detect
    ws_location_t location;  // Where the sensor physically sits
} sensor_config_t;

typedef struct {
    float temperature;
    float humidity;
    float pressure;
    float gas_resistance;
    char error_msg[128];
} sensor_reading_t;

// Parse i2c_addr field - returns 0x76, 0x77, or 0 for auto-detect
static int parse_i2c_addr(const char *ptr, const char *end) {
    char *addr_str = ws_json_parse_string(ptr, end, "i2c_addr");
    if (!addr_str) return 0;
    int addr = 0;
    if (strcmp(addr_str, "0x76") == 0) addr = 0x76;
    else if (strcmp(addr_str, "0x77") == 0) addr = 0x77;
    free(addr_str);
    return addr;
}

// Parse a simple JSON config file - returns dynamically allocated array
static sensor_config_t *load_config(const char *path, int *count) {
    char *buffer = NULL;
    const char *ptr;
    int sensor_idx = 0;
    sensor_config_t *configs = NULL;
    int sensor_count;
    
    *count = 0;
    
    buffer = ws_read_file(path, NULL);
    if (!buffer) return NULL;
    
    sensor_count = ws_json_count_objects(buffer);
    if (sensor_count == 0) { free(buffer); return NULL; }
    
    configs = malloc(sensor_count * sizeof(sensor_config_t));
    if (!configs) { free(buffer); return NULL; }
    
    ptr = buffer;
    while ((ptr = strchr(ptr, '{')) != NULL && sensor_idx < sensor_count) {
        /* Matching brace, not the first one: a config entry may contain nested
           objects or braces inside string values. */
        const char *end = ws_json_object_end(ptr);
        if (!end) break;
        
        configs[sensor_idx].internal = ws_json_parse_bool(ptr, end, "internal", false);
        configs[sensor_idx].sensor_id = ws_json_parse_string(ptr, end, "sensor_id");
        configs[sensor_idx].sensor_name = ws_json_parse_string(ptr, end, "sensor_name");
        configs[sensor_idx].i2c_addr = parse_i2c_addr(ptr, end);
        ws_parse_sensor_location(ptr, end, &configs[sensor_idx].location);
        
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

// Build calibration object for a specific measurement type
static void build_calibration_json(char *buffer, size_t bufsize, 
    const char *measures, struct bme680_calib_data *calib) {
    if (!calib) {
        buffer[0] = '\0';
        return;
    }
    
    if (strcmp(measures, "temperature") == 0) {
        snprintf(buffer, bufsize,
            "{\"par_t1\":%u,\"par_t2\":%d,\"par_t3\":%d,\"t_fine\":%d}",
            calib->par_t1, calib->par_t2, calib->par_t3, calib->t_fine);
    } else if (strcmp(measures, "humidity") == 0) {
        snprintf(buffer, bufsize,
            "{\"par_h1\":%u,\"par_h2\":%u,\"par_h3\":%d,\"par_h4\":%d,\"par_h5\":%d,\"par_h6\":%u,\"par_h7\":%d,\"t_fine\":%d}",
            calib->par_h1, calib->par_h2, calib->par_h3, calib->par_h4, 
            calib->par_h5, calib->par_h6, calib->par_h7, calib->t_fine);
    } else if (strcmp(measures, "pressure") == 0) {
        snprintf(buffer, bufsize,
            "{\"par_p1\":%u,\"par_p2\":%d,\"par_p3\":%d,\"par_p4\":%d,\"par_p5\":%d,\"par_p6\":%d,\"par_p7\":%d,\"par_p8\":%d,\"par_p9\":%d,\"par_p10\":%u,\"t_fine\":%d}",
            calib->par_p1, calib->par_p2, calib->par_p3, calib->par_p4, calib->par_p5,
            calib->par_p6, calib->par_p7, calib->par_p8, calib->par_p9, calib->par_p10, calib->t_fine);
    } else if (strcmp(measures, "resistance") == 0) {
        snprintf(buffer, bufsize,
            "{\"par_gh1\":%d,\"par_gh2\":%d,\"par_gh3\":%d,\"res_heat_range\":%u,\"res_heat_val\":%d,\"range_sw_err\":%d}",
            calib->par_gh1, calib->par_gh2, calib->par_gh3, 
            calib->res_heat_range, calib->res_heat_val, calib->range_sw_err);
    } else {
        buffer[0] = '\0';
    }
}

static void build_sensor_json(char *output, size_t output_len,
    const char *sensor, const char *measures, const char *unit,
    float value, int internal, const char *sensor_id,
    const char *sensor_name, const char *error_msg, time_t timestamp,
    struct bme680_calib_data *calib, int i2c_addr,
    const ws_location_t *location) {
    
    char config_obj[512];
    char calib_json[384];
    char addr_str[16];
    
    // Build base JSON with common fields
    if (ws_build_sensor_json_base(output, output_len,
                                   sensor, "bme680", measures, unit,
                                   sensor_id, sensor_name,
                                   internal, location, timestamp) != 0) {
        return;
    }
    
    // Build config object using helpers
    ws_build_config_base(config_obj, sizeof(config_obj), VERSION);
    
    if (i2c_addr > 0) {
        snprintf(addr_str, sizeof(addr_str), "0x%02X", i2c_addr);
        ws_config_add_string(config_obj, sizeof(config_obj), "i2c_addr", addr_str);
    }
    
    if (calib && i2c_addr > 0) {
        build_calibration_json(calib_json, sizeof(calib_json), measures, calib);
        if (calib_json[0] != '\0') {
            ws_config_add_object(config_obj, sizeof(config_obj), "calibration", calib_json);
        }
    }
    
    ws_config_end(config_obj);
    ws_sensor_json_set_config(output, output_len, config_obj);
    
    // Add value or error
    if (error_msg) {
        ws_sensor_json_set_error(output, error_msg);
    } else {
        ws_sensor_json_set_value(output, (double)value, 3);
    }
}

static void output_json(sensor_config_t *configs, int count, ws_location_filter_t location_filter) {
    size_t output_size = 4096;
    char *output = malloc(output_size);
    if (!output) { fprintf(stderr, "Memory allocation failed\n"); return; }
    strcpy(output, "[");
    int first = 1;
    
    // Open I2C device once
    int i2c_fd = open(DEFAULT_I2C_DEV, O_RDWR);
    
    for (int i = 0; i < count; i++) {
        sensor_reading_t reading = {0};
        struct bme680_calib_data calib = {0};
        int sensor_initialized = 0;
        char *sensor_id = configs[i].sensor_id ? strdup(configs[i].sensor_id) : ws_get_serial_with_suffix("bme680");
        const char *error_msg = NULL;
        time_t read_timestamp = time(NULL);
        if (location_filter == WS_LOCATION_INTERNAL && !configs[i].internal) { free(sensor_id); continue; }
        if (location_filter == WS_LOCATION_EXTERNAL && configs[i].internal) { free(sensor_id); continue; }
        
        // Read sensor - use configured address or auto-detect
        int addr = configs[i].i2c_addr;
        if (i2c_fd < 0) {
            snprintf(reading.error_msg, sizeof(reading.error_msg), "Failed to open I2C device");
            error_msg = reading.error_msg;
        } else {
            if (addr == 0) {
                addr = detect_i2c_address(i2c_fd);  // Auto-detect
            }
            if (addr < 0) {
                snprintf(reading.error_msg, sizeof(reading.error_msg), "BME680 not found at 0x76 or 0x77");
                error_msg = reading.error_msg;
            } else if (ioctl(i2c_fd, I2C_SLAVE, addr) < 0) {
                snprintf(reading.error_msg, sizeof(reading.error_msg), "Failed to set I2C address 0x%02X", addr);
                error_msg = reading.error_msg;
            } else if (bme680_init(i2c_fd, &calib) != 0) {
                snprintf(reading.error_msg, sizeof(reading.error_msg), "BME680 not found at 0x%02X%s", addr,
                         configs[i].i2c_addr ? " (specified in config)" : "");
                error_msg = reading.error_msg;
            } else if (bme680_read_data(i2c_fd, &calib, (struct bme680_data *)&reading) != 0) {
                snprintf(reading.error_msg, sizeof(reading.error_msg), "Failed to read sensor at 0x%02X", addr);
                error_msg = reading.error_msg;
            } else {
                sensor_initialized = 1;
            }
        }
        
        // Output JSON for temperature
        {
            char temp_json[2048];
            char *sensor_id_temp = malloc(strlen(sensor_id) + 16);
            snprintf(sensor_id_temp, strlen(sensor_id) + 16, "%s_temperature", sensor_id);
            build_sensor_json(temp_json, sizeof(temp_json),
                "bme680_temperature", "temperature", "Celsius",
                reading.temperature, configs[i].internal, sensor_id_temp,
                configs[i].sensor_name, error_msg, read_timestamp,
                sensor_initialized ? &calib : NULL, addr, &configs[i].location);
            size_t needed = strlen(output) + strlen(temp_json) + 3;
            if (needed > output_size) {
                output_size = needed * 2;
                char *new_output = realloc(output, output_size);
                if (!new_output) { fprintf(stderr, "Memory allocation failed\n"); free(output); return; }
                output = new_output;
            }
            if (!first) strcat(output, ",");
            strcat(output, temp_json);
            first = 0;
            free(sensor_id_temp);
        }
        
        // Output JSON for humidity
        {
            char humid_json[2048];
            char *sensor_id_humid = malloc(strlen(sensor_id) + 16);
            snprintf(sensor_id_humid, strlen(sensor_id) + 16, "%s_humidity", sensor_id);
            build_sensor_json(humid_json, sizeof(humid_json),
                "bme680_humidity", "humidity", "percentage",
                reading.humidity, configs[i].internal, sensor_id_humid,
                configs[i].sensor_name, error_msg, read_timestamp,
                sensor_initialized ? &calib : NULL, addr, &configs[i].location);
            size_t needed = strlen(output) + strlen(humid_json) + 3;
            if (needed > output_size) {
                output_size = needed * 2;
                char *new_output = realloc(output, output_size);
                if (!new_output) { fprintf(stderr, "Memory allocation failed\n"); free(output); return; }
                output = new_output;
            }
            if (!first) strcat(output, ",");
            strcat(output, humid_json);
            first = 0;
            free(sensor_id_humid);
        }
        
        // Output JSON for pressure
        {
            char press_json[2048];
            char *sensor_id_press = malloc(strlen(sensor_id) + 16);
            snprintf(sensor_id_press, strlen(sensor_id) + 16, "%s_pressure", sensor_id);
            build_sensor_json(press_json, sizeof(press_json),
                "bme680_pressure", "pressure", "hPa",
                reading.pressure / 100.0, configs[i].internal, sensor_id_press,
                configs[i].sensor_name, error_msg, read_timestamp,
                sensor_initialized ? &calib : NULL, addr, &configs[i].location);
            size_t needed = strlen(output) + strlen(press_json) + 3;
            if (needed > output_size) {
                output_size = needed * 2;
                char *new_output = realloc(output, output_size);
                if (!new_output) { fprintf(stderr, "Memory allocation failed\n"); free(output); return; }
                output = new_output;
            }
            if (!first) strcat(output, ",");
            strcat(output, press_json);
            first = 0;
            free(sensor_id_press);
        }
        
        // Output JSON for gas resistance
        {
            char gas_json[2048];
            char *sensor_id_gas = malloc(strlen(sensor_id) + 16);
            snprintf(sensor_id_gas, strlen(sensor_id) + 16, "%s_gas_resistance", sensor_id);
            build_sensor_json(gas_json, sizeof(gas_json),
                "bme680_gas", "resistance", "Ohms",
                reading.gas_resistance, configs[i].internal, sensor_id_gas,
                configs[i].sensor_name, error_msg, read_timestamp,
                sensor_initialized ? &calib : NULL, addr, &configs[i].location);
            size_t needed = strlen(output) + strlen(gas_json) + 3;
            if (needed > output_size) {
                output_size = needed * 2;
                char *new_output = realloc(output, output_size);
                if (!new_output) { fprintf(stderr, "Memory allocation failed\n"); free(output); return; }
                output = new_output;
            }
            if (!first) strcat(output, ",");
            strcat(output, gas_json);
            first = 0;
            free(sensor_id_gas);
        }
        free(sensor_id);
    }
    
    // Close I2C device once after all readings
    if (i2c_fd >= 0) close(i2c_fd);
    
    strcat(output, "]");
    printf("%s\n", output);
    free(output);
}

int main(int argc, char *argv[]) {
    sensor_config_t *configs = NULL;
    /* Zero-initialised: the default path sets each field explicitly except
       location, which must read as WS_LOC_UNDECLARED rather than whatever
       was on the stack. A garbage source of WS_LOC_EXPLICIT would emit a
       GeoJSON Point built from uninitialised coordinates. */
    sensor_config_t default_config = {0};
    int config_count = 0;
    ws_location_filter_t location_filter = WS_LOCATION_ALL;

    if (argc >= 2) {
        if (strcmp(argv[1], "identify") == 0) {
            ws_cmd_identify();
        } else if (strcmp(argv[1], "list") == 0) {
            static const char *measurements[] = {"temperature", "humidity", "pressure", "gas", NULL};
            ws_cmd_list_multiple(measurements);
        } else if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "version") == 0) {
            ws_print_version("sensor-bme680", VERSION);
            return WS_EXIT_SUCCESS;
        } else if (strcmp(argv[1], "enable") == 0) {
            /* BME680 uses I2C which is typically already enabled */
            printf("BME680 sensor uses I2C - ensure I2C is enabled in raspi-config.\n");
            return WS_EXIT_SUCCESS;
        } else if (strcmp(argv[1], "setup") == 0) {
            /* BME680 has no setup requirements beyond I2C */
            printf("BME680 sensor requires no additional setup.\n");
            return WS_EXIT_SUCCESS;
        } else if (strcmp(argv[1], "mock") == 0) {
            /* Output mock data for testing without hardware */
            char *serial = ws_get_serial_with_suffix("bme680_mock");
            time_t now = time(NULL);
            char json[2048];
            printf("[");
            /* Temperature */
            char temp_id[128];
            snprintf(temp_id, sizeof(temp_id), "%s_temperature", serial);
            if (ws_build_sensor_json_base(json, sizeof(json), "bme680_temperature", "bme680", "temperature", "Celsius",
                                          temp_id, "Mock BME680", false, NULL, now) == 0) {
                ws_sensor_json_set_value(json, 23.5, 3);
                printf("%s", json);
            }
            /* Humidity */
            char humid_id[128];
            snprintf(humid_id, sizeof(humid_id), "%s_humidity", serial);
            if (ws_build_sensor_json_base(json, sizeof(json), "bme680_humidity", "bme680", "humidity", "percentage",
                                          humid_id, "Mock BME680", false, NULL, now) == 0) {
                ws_sensor_json_set_value(json, 45.0, 3);
                printf(",%s", json);
            }
            /* Pressure */
            char press_id[128];
            snprintf(press_id, sizeof(press_id), "%s_pressure", serial);
            if (ws_build_sensor_json_base(json, sizeof(json), "bme680_pressure", "bme680", "pressure", "hPa",
                                          press_id, "Mock BME680", false, NULL, now) == 0) {
                ws_sensor_json_set_value(json, 1013.25, 3);
                printf(",%s", json);
            }
            /* Gas */
            char gas_id[128];
            snprintf(gas_id, sizeof(gas_id), "%s_gas_resistance", serial);
            if (ws_build_sensor_json_base(json, sizeof(json), "bme680_gas", "bme680", "resistance", "Ohms",
                                          gas_id, "Mock BME680", false, NULL, now) == 0) {
                ws_sensor_json_set_value(json, 50000.0, 3);
                printf(",%s", json);
            }
            printf("]\n");
            free(serial);
            return WS_EXIT_SUCCESS;
        } else if (strcmp(argv[1], "internal") == 0) {
            location_filter = WS_LOCATION_INTERNAL;
        } else if (strcmp(argv[1], "external") == 0) {
            location_filter = WS_LOCATION_EXTERNAL;
        } else if (strcmp(argv[1], "all") != 0) {
            fprintf(stderr, "Unknown command: %s\n", argv[1]);
            fprintf(stderr, "Usage: sensor-bme680 [--version|identify|list|setup|enable|mock|internal|external|all]\n");
            return WS_EXIT_INVALID_ARG;
        }
    }

    configs = load_config(CONFIG_PATH, &config_count);
    if (configs == NULL || config_count == 0) {
        char *serial = ws_get_serial_with_suffix("bme680");
        default_config.internal = 0;
        default_config.sensor_id = serial;
        default_config.sensor_name = NULL;
        default_config.i2c_addr = 0;  // 0 = auto-detect
        configs = &default_config;
        config_count = 1;
    }

    output_json(configs, config_count, location_filter);

    if (configs == &default_config) {
        free(default_config.sensor_id);
        free(default_config.sensor_name);
    } else {
        free_config(configs, config_count);
    }
    return WS_EXIT_SUCCESS;
}
