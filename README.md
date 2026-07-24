# sensor-mcu: sensor logger + safety watchdog for a spray pyrolysis rig

Firmware for a standalone **ESP32-S3** that sits next to a spray pyrolysis
slider. The rig's motion and spray valve are driven by a *separate* ESP32-S3
running [FluidNC]. This board does exactly two jobs:

1. **Logging**: stream every process sensor as CSV over USB serial.
2. **Safety cutoff**: watch the safety-critical sensors and drive a fail-safe
   GPIO into FluidNC's `safety_door_pin` when a threshold is exceeded or a
   sensor dies.

> **Status: logging works, safety pending.** The board boots, blinks its status
> LED, runs the non-blocking scheduler, brings up both buses, reads all six
> sensors, and streams the 1 Hz CSV log line. Still to do: the safety cutoff
> logic and the serial commands. See [Roadmap](#roadmap).

## Hardware

Target board: **ESP32-S3 DevKitC-1** (3.3 V logic). All sensors are Adafruit
breakouts.

| Sensor | Bus | Role |
|---|---|---|
| MAX31856 + type-K thermocouple | SPI | Sacrificial substrate temp between passes, for cooldown tuning (logging) |
| MAX31865 + PT100 RTD | SPI | Second temperature point, **safety-critical** |
| SHT45 | I2C | Ambient temp/humidity (logging + SGP40 compensation) |
| SGP40 | I2C | Solvent vapor / VOC index (warning) |
| MPRLS | I2C | Spray-line pressure (warning) |
| ADS1115 | I2C | 4 spare analog channels (logging) |

## Wiring

Pins are defined in [`include/config.h`](include/config.h); change them there,
not here.

| Signal | ESP32-S3 pin |
|---|---|
| I2C SDA / SCL | GPIO 8 / 9 |
| SPI SCK / MISO / MOSI | GPIO 12 / 13 / 11 |
| MAX31856 CS | GPIO 10 |
| MAX31865 CS | GPIO 14 |
| SHUTDOWN → FluidNC | GPIO 4 |
| WARN | GPIO 5 |
| Status LED (onboard WS2812) | GPIO 38 |

> The status "LED" is a single onboard **WS2812 addressable RGB pixel**, not a
> plain GPIO LED, driven with Adafruit_NeoPixel. On this Waveshare-style board
> it's on GPIO 38 (the Espressif reference design uses GPIO 48).

## FluidNC cutoff contract (frozen)

`PIN_SHUTDOWN` (GPIO 4) + common ground → a spare FluidNC input:

```yaml
control:
  safety_door_pin: gpio.N:high:pu   # pick a free FluidNC gpio; :high:pu is required
```

Polarity is **fail-safe: LOW = OK, HIGH/floating = fault.** A cut wire or a dead
sensor MCU therefore reads as a fault and stops the machine (feed hold + spray
valve off). Both boards are 3.3 V, so direct wire, no level shifting.

Changing this contract means updating `config.h`, this README, and the physical
FluidNC config together.

## Requirements

- [PlatformIO](https://platformio.org/) (CLI or the VS Code extension).
- A USB-C cable to the board's **native USB** port (this board has no separate
  USB-UART bridge; flashing and serial both go over the ESP32-S3's built-in
  USB-Serial/JTAG).
- Library dependencies are declared in [`platformio.ini`](platformio.ini) and
  fetched automatically on first build (NeoPixel, MAX31856, MAX31865, SHT4x,
  SGP40, MPRLS, ADS1X15, BusIO, Unified Sensor).

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
- `[INIT]` / `[SAFETY]` / `[STATUS]`: everything else (boot banner, safety
  events, diagnostics).

```sh
pio device monitor | grep '^DATA' > run.csv    # capture just the data
```

## Firmware layout

| File | Responsibility |
|---|---|
| [`src/main.cpp`](src/main.cpp) | `setup()`/`loop()`, non-blocking millis() scheduler, serial UI |
| [`src/sensors.{h,cpp}`](src/sensors.h) | Bus init, per-sensor drivers, the `SensorReadings` schema |
| [`src/safety.{h,cpp}`](src/safety.h) | Thresholds, latching, cutoff-GPIO drive |
| [`include/config.h`](include/config.h) | Pin map, timing, **safety thresholds** |

The scheduler runs two independent ticks off `millis()` (no `delay()`):

- **fast tick** (`SAFETY_POLL_MS`, 250 ms): safety-critical reads + trip logic.
- **slow tick** (`LOG_PERIOD_MS`, 1000 ms): logging sensors + the CSV line.
  Keep this at 1 Hz: the SGP40 VOC algorithm expects that cadence.

## Thresholds are not commissioned

Every limit in the "Safety thresholds" section of `config.h` is a **placeholder**.
They must be tuned against your real process before the cutoff can be trusted.
Do not present them as validated values.

## Roadmap

Implemented so far: boot/flash workflow, status-LED heartbeat, the non-blocking
scheduler, I2C/SPI bus bring-up, all six sensor drivers (I2C + SPI, with
per-read fault/NaN handling), and the 1 Hz CSV `DATA` line. Remaining work,
roughly in dependency order:

1. **Logging polish**: boot I2C scan, CI.
2. **Safety subsystem**: threshold rules, WARN (non-latching) / SHUTDOWN
   (latching), dead-sensor detection, LED patterns, `STATUS`/`RESET`/`TEST`
   serial commands, bench verification.
3. **Rig integration & commissioning**: wire to FluidNC, integration test,
   tune thresholds against the real process.

### Library gotchas to remember

- **MAX31856**: the fault register clears on read and there's no `clearFault()`;
  use continuous-conversion mode (`MAX31856_CONTINUOUS`) because one-shot reads
  block ~200 ms.
- **MAX31865**: PT100 with the Adafruit breakout's **430 Ω** reference; set
  `RTD_WIRES` in `config.h` to match how the PT100 is actually wired.

[FluidNC]: https://github.com/bdring/FluidNC
