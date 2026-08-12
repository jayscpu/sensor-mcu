# sensor-mcu: sensor logger for a spray pyrolysis rig

Firmware for a standalone **ESP32-S3** that sits next to a spray pyrolysis
slider. The rig's motion and spray valve are driven by a *separate* ESP32-S3
running [FluidNC]. This board does one job: read the process sensors and
stream them, as a CSV line over USB serial and as JSON-over-UDP broadcast to
the CrowPanel pendant running FluidTouch, which displays them live and records
them alongside the machine's position and state.

> **Status: working.** The board boots, runs the non-blocking scheduler,
> brings up both buses, reads all five sensors, and ships readings at 1 Hz
> over both outputs. Real sensors are not yet wired; `SIMULATE_SENSORS` is on.

## Hardware

Target board: **ESP32-S3 DevKitC-1** (3.3 V logic). All sensors are Adafruit
breakouts.

| Sensor | Bus | Role |
|---|---|---|
| MAX31856 + type-K thermocouple | SPI | Sacrificial substrate temp between passes, for cooldown tuning |
| MAX31865 + PT100 RTD | SPI | Metal temp |
| SHT45 | I2C | Ambient temp/humidity (also feeds SGP40 compensation) |
| SGP40 | I2C | Solvent vapor / VOC index |
| MPRLS | I2C | Spray-line pressure |

An ADS1115 (4 spare analog channels) may be added later; its driver is not
built, but its gain setting and library dependency are kept around for when
it's re-added.

## Wiring

Pins are defined in [`include/config.h`](include/config.h); change them there,
not here.

| Signal | ESP32-S3 pin |
|---|---|
| I2C SDA / SCL | GPIO 8 / 9 |
| SPI SCK / MISO / MOSI | GPIO 12 / 13 / 11 |
| MAX31856 CS | GPIO 10 |
| MAX31865 CS | GPIO 14 |

There is no wired connection to any other board: the only outputs are USB
serial and WiFi.

## Requirements

- [PlatformIO](https://platformio.org/) (CLI or the VS Code extension).
- A USB-C cable to the board's **native USB** port (this board has no separate
  USB-UART bridge; flashing and serial both go over the ESP32-S3's built-in
  USB-Serial/JTAG).
- Library dependencies are declared in [`platformio.ini`](platformio.ini) and
  fetched automatically on first build (MAX31856, MAX31865, SHT4x, SGP40,
  MPRLS, ADS1X15, BusIO, Unified Sensor).

## Build and flash

```sh
pio run                    # build
pio run -t upload          # flash
pio device monitor         # serial at 115200 baud
```

`-DARDUINO_USB_CDC_ON_BOOT=1` is set in `platformio.ini` so `Serial` goes to the
native USB port from boot; without it, nothing printed is visible in a monitor.

## Serial output format

Two kinds of line, so a host can `grep` clean CSV out of the noise:

- `DATA,...`: one comma-separated row of readings at 1 Hz. Fixed column order:
  `tc_c, rtd_c, ambient_c, ambient_rh, voc_index, pressure_hpa`.
  An empty field means that sensor is unavailable.
- Bracket-prefixed lines: everything else. Today that's `[INIT]` (boot banner +
  sensor probe results; the boot banner repeats at 1 Hz by design, so a
  late-attaching monitor still sees it) and `[WIFI]` (connect/disconnect
  events).

```sh
pio device monitor | grep '^DATA' > run.csv    # capture just the data
```

## UDP telemetry (FluidTouch screen)

The receiver is the **FluidTouch pendant** (Elecrow CrowPanel 7"), which
displays live sensor tiles and records each packet as one CSV row on its SD
card, correlated with the machine's position and state. Its side of the
contract is already implemented (`SensorLink` in the FluidTouch repo); this
board conforms to it.

Each tick sends one JSON packet to the **subnet broadcast address** on
`UDP_PORT` (**5005**, must match `SENSOR_UDP_PORT` in the FluidTouch repo).
The rig uses a dedicated offline router; broadcast means neither board needs
to know the other's IP. Fire-and-forget: nothing on the screen can stall
sampling here.

```json
{"seq":1234,"ms":45678,"d":{"tc_c":351.42,"rtd_c":382.10,"ambient_c":25.03,"ambient_rh":40.5,"voc_index":112,"pressure_hpa":1013.2}}
```

- `seq`: sample counter, +1 per tick from boot. Load-bearing: it's how the
  screen tells a lost packet from a quiet sensor, and it counts every sample
  taken (not every packet sent), so WiFi dropouts on this end show up in the
  screen's lost-packet total. A backwards jump means this board rebooted.
- `ms`: this board's `millis()` at the sample.
- `d`: one key per channel. A channel whose sensor is unavailable this tick is
  **omitted** (the screen keeps its column slot and leaves the row blank).
  The screen discovers channel names at runtime, caps at 8 channels (we use
  6), and truncates names at 15 chars (all ours fit).

**Testing without sensors:** set `SIMULATE_SENSORS` to 1 in `config.h`
(currently on) and the board broadcasts plausible wandering fake readings in
the exact same packet format, with an occasional simulated SHT45 dropout so
the screen's channel-omission path gets exercised too. The probe output prints
a loud `SIMULATE_SENSORS=1: readings below are FAKE` warning. Set it back to 0
for real runs. Note the screen only starts listening after an operator selects
a FluidNC machine (it joins WiFi at that point), so connect it to the machine
before expecting tiles to move.

Set `WIFI_SSID` / `WIFI_PASS` in `config.h` to the rig router's credentials.
WiFi join is non-blocking with auto-reconnect: sensor reads and serial logging
run regardless, UDP sends are skipped while disconnected, and `[WIFI]` lines
on serial announce connects/drops. Broadcast frames aren't retried at the WiFi
layer, so the occasional packet drops; at 1 Hz telemetry each row is
superseded a second later, so that's acceptable by design.

## Firmware layout

| File | Responsibility |
|---|---|
| [`src/main.cpp`](src/main.cpp) | `setup()`/`loop()`, non-blocking millis() scheduler, CSV + UDP output |
| [`src/sensors.{h,cpp}`](src/sensors.h) | Bus init, per-sensor drivers, the `SensorReadings` schema |
| [`include/config.h`](include/config.h) | Pin map, WiFi credentials, timing |

The scheduler runs a single tick off `millis()` (no `delay()`):

- **tick** (`LOG_PERIOD_MS`, 1000 ms): read every sensor, then ship the CSV
  line and the UDP packet. Keep this at 1 Hz: the SGP40 VOC algorithm expects
  that cadence.

## Roadmap

Implemented so far: boot/flash workflow, the non-blocking single-tick
scheduler, I2C/SPI bus bring-up, all five sensor drivers (I2C + SPI, with
per-read fault/NaN handling), the 1 Hz CSV `DATA` line over USB serial, and
the JSON-over-UDP broadcast to the FluidTouch screen.
Remaining work, roughly in dependency order:

1. **Logging polish**: boot I2C scan, CI.
2. **Real-sensor bring-up**: wire the five sensors, set `SIMULATE_SENSORS`
   to 0, and verify every channel end to end on the FluidTouch screen.

### Library gotchas to remember

- **MAX31856**: the fault register clears on read and there's no `clearFault()`;
  use continuous-conversion mode (`MAX31856_CONTINUOUS`) because one-shot reads
  block ~200 ms.
- **MAX31865**: PT100 with the Adafruit breakout's **430 Ω** reference; set
  `RTD_WIRES` in `config.h` to match how the PT100 is actually wired.

[FluidNC]: https://github.com/bdring/FluidNC
