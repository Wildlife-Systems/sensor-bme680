# sensor-bme680

A C implementation for reading BME680 environmental sensors on Raspberry Pi via I2C.

## Features

- Temperature, humidity, pressure, and gas resistance measurements
- I2C communication with configurable address
- Multiple sensor support via configuration file
- Internal/external location filtering
- Mock mode for testing without hardware

## Building from source

```bash
make
```

## Installing

### From source

```bash
sudo make install
```

### From Debian package

[Add the WildlifeSystems APT repository to your system](https://wildlife.systems/apt-configuration.html)

```bash
sudo apt update
sudo apt install sensor-bme680
```

## Usage

### Enable I2C (first-time setup)

```bash
sudo sensor-bme680 enable
```

This reminds you to enable I2C via `raspi-config`. Reboot if required.

### Read sensors

```bash
# Read all measurements (temperature, humidity, pressure, gas)
sensor-bme680

# Read only internal sensors
sensor-bme680 internal

# Read only external sensors
sensor-bme680 external

# List available measurement types
sensor-bme680 list

# Identify (exits with code 60)
sensor-bme680 identify

# Show version
sensor-bme680 version

# Output mock data for testing
sensor-bme680 mock
```

## Configuration

Configuration is read from `/etc/ws/sensors/bme680.json`. Example:

```json
[
  {
    "address": "0x76",
    "internal": false
  }
]
```

### Configuration options

- `address`: I2C address (0x76 or 0x77)
- `internal`: Boolean indicating if sensor is inside the enclosure
- `sensor_id`: Optional custom sensor ID

## Output

The program outputs JSON in the WildlifeSystems sensor format:

```json
[
  {"sensor":"bme680_temperature","measures":"temperature","unit":"Celsius","value":23.5,"internal":false,"sensor_id":"1234567890abcdef_bme680_temperature"},
  {"sensor":"bme680_humidity","measures":"humidity","unit":"percentage","value":45.0,"internal":false,"sensor_id":"1234567890abcdef_bme680_humidity"},
  {"sensor":"bme680_pressure","measures":"pressure","unit":"hPa","value":1013.25,"internal":false,"sensor_id":"1234567890abcdef_bme680_pressure"},
  {"sensor":"bme680_gas","measures":"resistance","unit":"Ohms","value":50000.0,"internal":false,"sensor_id":"1234567890abcdef_bme680_gas_resistance"}
]
```

## Requirements

- Raspberry Pi with I2C enabled
- BME680 sensor connected to I2C bus

## Exit codes

- `0`: Success
- `20`: Invalid argument
- `60`: Identify command

## Author

Ed Baker <ed@ebaker.me.uk>

## Project

Part of the WildlifeSystems project. For more information, visit:
- https://wildlife.systems
- https://docs.wildlife.systems
