# sensor-mcu: sensors reader and logger

Standalone sensors **ESP32-S3** (DevKitC-1). Once a second it reads the
process sensors and broadcasts them as one JSON UDP packet to the CrowPanel
touchscreen running FluidTouch, which displays them live and records them
alongside the machine's position and state.

`SIMULATE_SENSORS` is on (`config.h`): the board sends fake wandering readings
until the real sensors are wired. Set it to 0 for real runs.

## Hardware

I2C on GPIO 8/9 (SDA/SCL), SPI on GPIO 12/13/11 (SCK/MISO/MOSI); all pins in
[`include/config.h`](include/config.h).

| Sensor | Bus | Measures |
|---|---|---|
| MAX31856 + type-K thermocouple | SPI, CS 10 | Substrate temp |
| MAX31865 + PT100 RTD | SPI, CS 14 | Metal temp |
| SHT45 | I2C | Ambient temp/humidity |
| SGP40 | I2C | VOC index |
| MPRLS | I2C | Spray-line pressure |

## Build and flash

Requires [PlatformIO]; USB-C to the board's native USB port.

```sh
pio run -t upload
```

## UDP packet

One broadcast packet per second to port **5005** (must match
`SENSOR_UDP_PORT` in `touchscreen/include/config.h`). WiFi credentials are in
`config.h`.

```json
{"seq":1234,"ms":45678,"d":{"tc_c":351.42,"rtd_c":382.10,"ambient_c":25.03,"ambient_rh":40.5,"voc_index":112,"pressure_hpa":1013.2,"status":31}}
```

- `seq`: +1 per sample, even while WiFi is down, so outages count as lost
  packets on the screen. A backwards jump = this board rebooted.
- `d`: one key per channel; unhealthy channels are omitted.
- `status`: health bitmask, in every packet: bit0 SHT45, bit1 SGP40,
  bit2 MPRLS, bit3 MAX31856, bit4 MAX31865; 31 = all OK. One bad read drops a
  bit immediately; 3 consecutive good reads restore it.

## Notes

- Keep the tick at 1 Hz (`LOG_PERIOD_MS`): the SGP40 VOC algorithm expects it.
- MAX31865: set `RTD_WIRES` to match how the PT100 is wired.
- The screen only listens after an operator selects a FluidNC machine.

[PlatformIO]: https://platformio.org/
