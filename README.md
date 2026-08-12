# sensor-mcu: sensor logger for a spray pyrolysis rig

Standalone sensors **ESP32-S3** . Once a second it reads the process
sensors and streams them two ways: a CSV line over USB serial, and a JSON UDP
broadcast to the CrowPanel touchscreen running FluidTouch, which displays them
live and records them alongside the machine's position and state.

**Status: working.** Real sensors are not wired yet; `SIMULATE_SENSORS` is on,
so the board broadcasts fake wandering readings for end-to-end testing.

## Hardware

Target: **ESP32-S3 DevKitC-1** (3.3 V). All sensors are Adafruit breakouts.
I2C on GPIO 8/9 (SDA/SCL); SPI on GPIO 12/13/11 (SCK/MISO/MOSI). Pins live in
[`include/config.h`](include/config.h). The only outputs are USB serial and WiFi.

| Sensor | Bus | Measures |
|---|---|---|
| MAX31856 + type-K thermocouple | SPI, CS 10 | Sacrificial substrate temp (cooldown tuning) |
| MAX31865 + PT100 RTD | SPI, CS 14 | Metal temp |
| SHT45 | I2C | Ambient temp/humidity (feeds SGP40 compensation) |
| SGP40 | I2C | Solvent vapor / VOC index |
| MPRLS | I2C | Spray-line pressure |

## Build and flash

Requires [PlatformIO] and a USB-C cable to the board's **native USB** port.
Dependencies fetch automatically on first build.

```sh
pio run                    # build
pio run -t upload          # flash
pio device monitor         # serial at 115200 baud
```

## Serial output

- `DATA,tc_c,rtd_c,ambient_c,ambient_rh,voc_index,pressure_hpa` at 1 Hz.
  An empty field means that sensor is unavailable.
- `[INIT]` / `[WIFI]` lines: everything else. The boot banner repeats at 1 Hz
  on purpose (native USB re-enumerates on reset, so a one-shot print would be
  missed).

```sh
pio device monitor | grep '^DATA' > run.csv
```

## UDP telemetry (FluidTouch)

One packet per tick to the subnet broadcast address on `UDP_PORT` (**5005**,
must match `SENSOR_UDP_PORT` in `touchscreen/include/config.h`). Set `WIFI_SSID` /
`WIFI_PASS` in `config.h`; join is non-blocking with auto-reconnect, and
sampling never waits on the network.

```json
{"seq":1234,"ms":45678,"d":{"tc_c":351.42,"rtd_c":382.10,"ambient_c":25.03,"ambient_rh":40.5,"voc_index":112,"pressure_hpa":1013.2}}
```

- `seq`: +1 per sample from boot, even when WiFi is down, so outages show in
  the screen's lost-packet count. A backwards jump = this board rebooted.
- `ms`: this board's `millis()`.
- `d`: one key per channel; unavailable channels are **omitted**. The screen
  discovers names at runtime (max 8 channels, 15-char names; we use 6).

**Testing:** with `SIMULATE_SENSORS 1` (in `config.h`) the packets carry fake
drifting values in the exact same format, with occasional SHT45 dropouts to
exercise the missing-channel path; boot output warns loudly that data is fake.
The screen only listens after an operator selects a FluidNC machine.

## Notes

- Keep the tick at 1 Hz (`LOG_PERIOD_MS`): the SGP40 VOC algorithm expects it.
- MAX31856 runs in continuous-conversion mode (one-shot reads block ~200 ms);
  its fault register clears on read, there is no `clearFault()`.
- MAX31865: 430 ohm reference on the Adafruit breakout; set `RTD_WIRES` to
  match the PT100 wiring.
- The rig router is offline: no NTP, so there are no wall-clock timestamps
  anywhere in the pipeline.

## Roadmap

1. **Logging polish**: boot I2C scan, CI.
2. **Real-sensor bring-up**: wire the five sensors, set `SIMULATE_SENSORS`
   to 0, verify every channel end to end on the touchscreen.

[FluidNC]: https://github.com/bdring/FluidNC
[PlatformIO]: https://platformio.org/
