# sensor-mcu: sensor logger + safety watchdog for a spray pyrolysis rig

Firmware for a standalone **ESP32-S3** that sits next to a spray pyrolysis
slider. The rig's motion and spray valve are driven by a *separate* ESP32-S3
running [FluidNC]. This board does exactly two jobs:

1. **Logging** (implemented): stream every process sensor as CSV over USB serial.
2. **Safety cutoff** (planned): watch the safety-critical sensors and drive a
   fail-safe GPIO into FluidNC's `safety_door_pin` when a threshold is exceeded
   or a sensor dies.

> **Status: logging works, safety not implemented.** The board boots, runs the
> non-blocking scheduler, brings up both buses, reads all five sensors, and
> streams readings at 1 Hz as a CSV line over USB serial and as JSON-over-UDP
> broadcast to the FluidTouch screen on the rig's offline WiFi network. There
> is no safety code yet:
> the SHUTDOWN/WARN pins are never driven, and the thresholds in `config.h` are
> unused placeholders. See [Roadmap](#roadmap).

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
| SHUTDOWN → FluidNC | GPIO 4 |
| WARN | GPIO 5 |

## FluidNC cutoff contract (frozen)

**Not implemented in firmware yet.** The contract below is the frozen design
for the planned safety subsystem. Until it's implemented the firmware never
drives `PIN_SHUTDOWN`, and with the fail-safe polarity a floating pin reads as
a fault, so wiring it to FluidNC today would just hold the machine in a
permanent fault state.

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
  events). The safety subsystem will add `[SAFETY]` / `[STATUS]`.

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
| [`src/main.cpp`](src/main.cpp) | `setup()`/`loop()`, non-blocking millis() scheduler, the CSV line |
| [`src/sensors.{h,cpp}`](src/sensors.h) | Bus init, per-sensor drivers, the `SensorReadings` schema |
| [`include/config.h`](include/config.h) | Pin map, timing, safety thresholds (placeholders for the planned safety subsystem) |

The scheduler runs a single tick off `millis()` (no `delay()`):

- **tick** (`LOG_PERIOD_MS`, 1000 ms): read every sensor, then print the CSV
  line. Keep this at 1 Hz: the SGP40 VOC algorithm expects that cadence. If
  the safety subsystem needs faster reaction than 1 s, the safety-critical
  reads (RTD, pressure) will get their own faster tick again.

## Thresholds are not commissioned

Every limit in the "Safety thresholds" section of `config.h` is a **placeholder**.
They must be tuned against your real process before the cutoff can be trusted.
Do not present them as validated values.

## Roadmap

Implemented so far: boot/flash workflow, the non-blocking single-tick
scheduler, I2C/SPI bus bring-up, all five sensor drivers (I2C + SPI, with
per-read fault/NaN handling), the 1 Hz CSV `DATA` line over USB serial, and
the JSON-over-UDP broadcast to the FluidTouch screen.
Remaining work, roughly in dependency order:

1. **Logging polish**: boot I2C scan, CI.
2. **Safety subsystem**: threshold rules, WARN (non-latching) / SHUTDOWN
   (latching), dead-sensor detection, serial commands, bench
   verification. The planned serial commands are operator tools typed into the
   serial monitor: `STATUS` prints a one-off snapshot of readings and safety
   state, `RESET` clears a latched shutdown after a trip (the alternative is a
   power cycle), and `TEST` forces a fake trip to verify the FluidNC wiring
   actually stops the machine.
3. **Rig integration & commissioning**: wire to FluidNC, integration test,
   tune thresholds against the real process.

### Library gotchas to remember

- **MAX31856**: the fault register clears on read and there's no `clearFault()`;
  use continuous-conversion mode (`MAX31856_CONTINUOUS`) because one-shot reads
  block ~200 ms.
- **MAX31865**: PT100 with the Adafruit breakout's **430 Ω** reference; set
  `RTD_WIRES` in `config.h` to match how the PT100 is actually wired.

[FluidNC]: https://github.com/bdring/FluidNC
