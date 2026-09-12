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

// The fields every driver shares live in base; i2c_addr is ours alone.
typedef struct {
    ws_sensor_config_base_t base;
    int i2c_addr;  // 0x76, 0x77, or 0 for auto-detect
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

static void free_config(sensor_config_t *configs, int count);

// What tells one BME680 from another when neither has a configured id: the
// I2C address it is read from. Two entries left to auto-detect cannot be told
// apart, and in truth both read whichever chip is found first; they get the
// same id and the library warns about it.
static void addr_designation(const void *entry, char *buf, size_t cap) {
    int addr = ((const sensor_config_t *)entry)->i2c_addr;
    if (addr > 0) {
        snprintf(buf, cap, "0x%02x", addr);
    } else {
        snprintf(buf, cap, "auto");
    }
}

// Parse a simple JSON config file - returns dynamically allocated array
static sensor_config_t *load_config(const char *path, int *count) {
    ws_config_iter_t it;
    sensor_config_t *configs;
    const char *entry, *entry_end;
    int n, idx = 0;

    *count = 0;

    n = ws_config_iter_open(&it, path);
    if (n <= 0) {
        ws_config_iter_close(&it);
        return NULL;
    }

    configs = calloc((size_t)n, sizeof(*configs));
    if (!configs) {
        ws_config_iter_close(&it);
        return NULL;
    }

    while (ws_config_iter_next(&it, &configs[idx].base, &entry, &entry_end)) {
        configs[idx].i2c_addr = parse_i2c_addr(entry, entry_end);
        idx++;
    }

    ws_config_iter_close(&it);

    // Entries without a sensor_id get one from the node serial. The library
    // keeps a lone entry at "<serial>_bme680", as the default config has
    // always produced, and tells two or more apart by I2C address.
    if (ws_config_assign_fallback_ids(configs, sizeof(*configs), idx, "bme680",
                                      addr_designation) < 0) {
        free_config(configs, idx);
        return NULL;
    }

    *count = idx;
    return configs;
}

static void free_config(sensor_config_t *configs, int count) {
    if (configs) {
        for (int i = 0; i < count; i++) {
            ws_sensor_config_free_fields(&configs[i].base);
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

// Returns 0 on success, -1 if the reading could not be built.
static int build_sensor_json(char *output, size_t output_len,
    const char *sensor, const char *measures, const char *unit,
    float value, int internal, const char *sensor_id,
    const char *sensor_name, const char *error_msg, time_t timestamp,
    struct bme680_calib_data *calib, int i2c_addr,
    const ws_location_t *location) {

    char config_obj[512];
    char calib_json[384];
    char addr_str[16];

    // Build base JSON with common fields. The strings go in raw: the
    // library escapes them.
    if (ws_build_sensor_json_base(output, output_len,
                                   sensor, "bme680", measures, unit,
                                   sensor_id, sensor_name,
                                   internal, location, timestamp) != 0) {
        ws_log_error("Could not build reading for %s", sensor);
        return -1;
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
    
    // Exactly one of value or error
    ws_sensor_json_set_result(output, output_len, (double)value, 3, error_msg);
    return 0;
}

// Build "<sensor_id>_<measurement>". Returns NULL if the id is unknown or the
// allocation fails; the caller then emits the reading with a null sensor_id
// rather than dereferencing NULL.
static char *measurement_id(const char *sensor_id, const char *measurement) {
    size_t len;
    char *out;

    if (!sensor_id) return NULL;

    len = strlen(sensor_id) + strlen(measurement) + 2;  // '_' and terminator
    out = malloc(len);
    if (!out) return NULL;

    snprintf(out, len, "%s_%s", sensor_id, measurement);
    return out;
}

// Append one measurement of one sensor to the output array. The sensor_id
// suffix is given separately because the gas reading measures "resistance" but
// is identified as "<id>_gas_resistance".
static void append_reading(ws_json_array_builder_t *out, const char *sensor_id,
                           const sensor_config_t *config, const char *sensor,
                           const char *measures, const char *id_suffix,
                           const char *unit, float value, const char *error_msg,
                           time_t timestamp, struct bme680_calib_data *calib,
                           int i2c_addr) {
    char json[2048];
    // An unknown id leaves "sensor_id":null, which records that it is
    // unknown. Dropping the reading instead would report the node as
    // having no sensors, which is worse and silent.
    char *id = measurement_id(sensor_id, id_suffix);

    // A reading that could not be built is not added: the array would
    // refuse the empty item anyway, and fail as a whole.
    if (build_sensor_json(json, sizeof(json), sensor, measures, unit, value,
                          config->base.internal, id, config->base.sensor_name,
                          error_msg, timestamp, calib, i2c_addr,
                          &config->base.location) == 0) {
        ws_json_array_add(out, json);
    }
    free(id);
}

// A NULL filter means every measurement.
static bool wanted(const char *filter, const char *measurement) {
    return !filter || strcmp(filter, measurement) == 0;
}

// Returns WS_EXIT_SUCCESS, or WS_EXIT_INVALID_ARG with nothing printed if the
// array could not be built: no output means "could not report", where "[]"
// would mean "no sensors".
static int output_json(sensor_config_t *configs, int count, const char *filter,
                       ws_location_filter_t location_filter) {
    ws_json_array_builder_t out;
    const char *json;
    int i2c_fd;
    int i;

    if (ws_json_array_init(&out) != 0) {
        ws_log_error("Out of memory building readings");
        return WS_EXIT_INVALID_ARG;
    }

    // Open I2C device once
    i2c_fd = open(DEFAULT_I2C_DEV, O_RDWR);

    for (i = 0; i < count; i++) {
        sensor_reading_t reading = {0};
        struct bme680_calib_data calib = {0};
        struct bme680_calib_data *calib_out = NULL;
        const char *error_msg = NULL;
        time_t read_timestamp = time(NULL);
        const char *sensor_id;
        int addr;

        if (location_filter == WS_LOCATION_INTERNAL && !configs[i].base.internal) continue;
        if (location_filter == WS_LOCATION_EXTERNAL && configs[i].base.internal) continue;

        // Assigned at load time when the config omitted it; NULL only when
        // the node has no serial, and then reported as null.
        sensor_id = configs[i].base.sensor_id;

        // Read sensor - use configured address or auto-detect
        addr = configs[i].i2c_addr;
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
                calib_out = &calib;
            }
        }

        if (wanted(filter, "temperature"))
            append_reading(&out, sensor_id, &configs[i], "bme680_temperature",
                       "temperature", "temperature", WS_UNIT_CELSIUS,
                       reading.temperature, error_msg, read_timestamp, calib_out, addr);
        if (wanted(filter, "humidity"))
            append_reading(&out, sensor_id, &configs[i], "bme680_humidity",
                       "humidity", "humidity", WS_UNIT_PERCENTAGE,
                       reading.humidity, error_msg, read_timestamp, calib_out, addr);
        if (wanted(filter, "pressure"))
            append_reading(&out, sensor_id, &configs[i], "bme680_pressure",
                       "pressure", "pressure", WS_UNIT_HPA,
                       reading.pressure / 100.0f, error_msg, read_timestamp, calib_out, addr);
        if (wanted(filter, "gas"))
            append_reading(&out, sensor_id, &configs[i], "bme680_gas_resistance",
                       "resistance", "gas_resistance", WS_UNIT_OHMS,
                       reading.gas_resistance, error_msg, read_timestamp, calib_out, addr);
    }

    // Close I2C device once after all readings
    if (i2c_fd >= 0) close(i2c_fd);

    ws_json_array_end(&out);
    json = ws_json_array_get(&out);
    if (!json) {
        ws_log_error("Out of memory building readings");
        ws_json_array_free(&out);
        return WS_EXIT_INVALID_ARG;
    }

    printf("%s\n", json);
    ws_json_array_free(&out);
    return WS_EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    // What this driver measures: the one source for the list command,
    // the measurement filters it accepts, and its usage line.
    static const char *measurements[] = {"temperature", "humidity", "pressure", "gas", NULL};
    const char *filter = NULL;
    sensor_config_t *configs = NULL;
    /* Zero-initialised: the default path sets each field explicitly except
       location, which must read as WS_LOC_UNDECLARED rather than whatever
       was on the stack. A garbage source of WS_LOC_EXPLICIT would emit a
       GeoJSON Point built from uninitialised coordinates. */
    sensor_config_t default_config = {0};
    int config_count = 0;
    ws_location_filter_t location_filter = WS_LOCATION_ALL;
    int status;

    /* So the library's warnings reach syslog under this driver's name, as
       sensor-dht11's do. */
    ws_log_init("sensor-bme680");

    if (argc >= 2) {
        if (strcmp(argv[1], "identify") == 0) {
            ws_cmd_identify();
        } else if (strcmp(argv[1], "list") == 0) {
            ws_cmd_list_multiple(measurements);
        } else if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "version") == 0) {
            ws_print_version("sensor-bme680", VERSION);
            return WS_EXIT_SUCCESS;
        } else if (strcmp(argv[1], "enable") == 0) {
            /* The kernel exposes no I2C bus until this is set, so enable it
               rather than telling the user to go and run raspi-config. */
            return ws_cmd_enable_boot_config("dtparam=i2c_arm=on",
                                             "dtparam=i2c_arm",
                                             "I2C interface",
                                             "sensor-bme680");
        } else if (strcmp(argv[1], "setup") == 0) {
            /* BME680 has no setup requirements beyond I2C */
            printf("BME680 sensor requires no additional setup.\n");
            return WS_EXIT_SUCCESS;
        } else if (strcmp(argv[1], "mock") == 0) {
            // Fixed readings in the real output format, for testing without
            // hardware. The values are ours; the formatting is the library's,
            // so mock cannot drift from what a real read produces.
            static const ws_mock_reading_t mock[] = {
                // Declared at the node, as sensor-onboard's mock declares its
                // physical sensors, so every driver's mock has one shape.
                { "bme680_temperature",      "temperature", NULL,             WS_UNIT_CELSIUS,      23.5, 3, "{{node}}" },
                { "bme680_humidity",         "humidity",    NULL,             WS_UNIT_PERCENTAGE,   45.0, 3, "{{node}}" },
                { "bme680_pressure",         "pressure",    NULL,             WS_UNIT_HPA,        1013.25, 3, "{{node}}" },
                { "bme680_gas_resistance",   "resistance",  "gas_resistance", WS_UNIT_OHMS,      50000.0, 3, "{{node}}" },
            };
            return ws_cmd_mock("bme680", "bme680_mock", "Mock BME680",
                               mock, sizeof(mock) / sizeof(mock[0]));
        } else if (ws_arg_is_measurement(argv[1], measurements)) {
            filter = argv[1];
        } else if (strcmp(argv[1], "internal") == 0) {
            location_filter = WS_LOCATION_INTERNAL;
        } else if (strcmp(argv[1], "external") == 0) {
            location_filter = WS_LOCATION_EXTERNAL;
        } else if (strcmp(argv[1], "all") != 0) {
            return ws_cmd_unknown_arg("sensor-bme680", argv[1], measurements);
        }
    }

    /* Every reading needs the template, so ask once before touching the
       sensor. Without it, fail with nothing printed: "[]" would claim the
       node has no sensors, and a partial array is not JSON at all. */
    status = ws_require_prototype();
    if (status != 0) return status;

    configs = load_config(CONFIG_PATH, &config_count);
    if (configs == NULL || config_count == 0) {
        char *serial = ws_get_serial_with_suffix("bme680");
        default_config.base.internal = false;
        default_config.base.sensor_id = serial;
        default_config.base.sensor_name = NULL;
        default_config.i2c_addr = 0;  // 0 = auto-detect
        configs = &default_config;
        config_count = 1;
    }

    status = output_json(configs, config_count, filter, location_filter);

    if (configs == &default_config) {
        free(default_config.base.sensor_id);
        free(default_config.base.sensor_name);
    } else {
        free_config(configs, config_count);
    }
    return status;
}
