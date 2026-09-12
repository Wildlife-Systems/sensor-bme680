# sensor-bme680

sensor-bme680 reads Bosch BME680 environmental sensors connected to a Raspberry
Pi over I2C and outputs their readings in the WildlifeSystems format. It is
written in C.

## Features

- Temperature, humidity, pressure and gas resistance measurements
- I2C address configurable, or detected automatically
- Multiple sensors via the configuration file
- Filtering of internal and external sensors
- A mock mode for testing without hardware

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

[Add the WildlifeSystems APT repository to the system](https://wildlife.systems/apt-configuration.html), then install the package.

```bash
sudo apt update
sudo apt install sensor-bme680
```

## Usage

### Enable I2C

The kernel exposes no I2C bus until it is enabled in the Raspberry Pi boot
configuration. The `enable` command adds the required directive, or reports
that it is already present. A reboot is required after the directive is first
added.

```bash
sudo sensor-bme680 enable
```

### Read sensors

Not specifying a command reads every measurement from every sensor.

```bash
# Read all measurements (temperature, humidity, pressure, gas)
sensor-bme680

# Read one measurement
sensor-bme680 temperature
sensor-bme680 humidity
sensor-bme680 pressure
sensor-bme680 gas

# Read only internal, or only external, sensors
sensor-bme680 internal
sensor-bme680 external

# List the available measurements
sensor-bme680 list

# Identify (exits with code 60)
sensor-bme680 identify

# Show the version
sensor-bme680 version

# Output mock readings for testing
sensor-bme680 mock
```

The `setup` command reports that no setup is required. It is provided so that
every WildlifeSystems driver answers the same commands.

## Configuration

Configuration is read from `/etc/ws/sensors/bme680.json`. Without the file, a
single sensor is detected automatically at address 0x76 or 0x77.

```json
[
  {
    "i2c_addr": "0x76",
    "internal": false,
    "sensor_name": "Weather station",
    "location": "{{node}}"
  }
]
```

An example showing every option in use is installed as
`/usr/share/doc/sensor-bme680/examples/bme680.json`; it is valid JSON and can
be copied into place and edited.

### Configuration options

- `i2c_addr`: the I2C address, `"0x76"` or `"0x77"`. If omitted, the address
  is detected automatically.
- `internal`: whether the sensor is inside the enclosure. The default is
  false.
- `sensor_id`: a custom sensor identifier. If omitted, the identifier is
  `<serial>_bme680`, where `<serial>` is the Raspberry Pi serial number. Where
  more than one entry omits it, each is instead `<serial>_bme680_0x76` or
  `<serial>_bme680_0x77`, so that two sensors do not share an identifier. Each
  measurement appends its own name, e.g. `<serial>_bme680_temperature`.
- `sensor_name`: a human-readable name, reported in the `sensor_name` field.
- `location`: where the sensor is. Either `"{{node}}"` for the position of the
  node, `"{{none}}"` for a sensor that has no position, or an object with
  `latitude` and `longitude` in decimal degrees and, optionally, `altitude`
  and `accuracy` in metres. If omitted, the `location` field of the reading is
  null.

## Output

The program outputs a JSON array in the WildlifeSystems format with one reading
per measurement. The `node_id` and `deployment_id` fields are filled in by
`sr`.

```json
{"sensor":"bme680_temperature","device":"bme680","measures":"temperature","value":23.500,"unit":"Celsius","node_id":null,"sensor_id":"1234567890abcdef_bme680_temperature","sensor_name":null,"location":null,"deployment_id":null,"timestamp":1789225958,"config":{"software_version":"2.3.0","i2c_addr":"0x76","calibration":{"par_t1":26123,"par_t2":26421,"par_t3":3,"t_fine":118912}},"internal":false,"error":null}
```

The four readings are `bme680_temperature` (Celsius), `bme680_humidity`
(percentage), `bme680_pressure` (hPa) and `bme680_gas_resistance` (Ohms, which
measures `resistance`). A reading that could not be taken has a null `value`
and the reason in `error`. The `config` object carries the calibration
parameters used for the measurement.

## Requirements

- A Raspberry Pi with I2C enabled (see `sensor-bme680 enable`)
- A BME680 sensor connected to I2C bus 1

## Exit codes

- `0`: success
- `20`: an invalid argument, or the readings could not be produced at all (for
  example, `sc-prototype` was unavailable). Nothing is printed in that case.
- `60`: the `identify` command

## Author

Ed Baker <ed@ebaker.me.uk>

## Project

Part of the WildlifeSystems project. For more information, visit:
- https://wildlife.systems
- https://docs.wildlife.systems
