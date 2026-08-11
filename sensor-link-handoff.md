# Handoff: getting the sensor board talking to the screen

**Status:** the screen side is written and compiles for both board variants. It
has **never been flashed and never been tested against a real sensor board.**
Everything below the "Verify" heading is therefore a procedure, not a report.

**Your job:** flash the transmitter to the sensor ESP, replace the placeholder
sensor reads with the real ones, and confirm readings arrive, display, and land
in a CSV on the screen's SD card.

---

## 1. The system

Three separate ESP32s. Only two of them are built from this repo.

| Node | What it is | Talks to |
|---|---|---|
| **Screen** | This repo. Elecrow CrowPanel 7" (ESP32-S3, 8 MB PSRAM). | FluidNC over WebSocket; sensor board over UDP |
| **FluidNC** | The motion controller running the G-code. Separate firmware, not in this repo. | Screen |
| **Sensor board** | Its own ESP with the process sensors. Was fully decoupled until this work. | Screen (one-way) |

The rig itself is a CNC gantry carrying a spray head over a **fixed hotplate**.
The hotplate never moves — X, Y and Z all move the sprayer, and A is a syringe
pump. Do not let any UI text imply the sample moves; that mistake was made once
already and had to be corrected throughout.

**Why the screen records the data.** It is the only node that sees the sensor
stream *and* the machine's live position and state, so it is the only place the
two can be put on the same row. That correlation is the entire reason recording
happens on the pendant rather than on the sensor board.

---

## 2. The contract between the two boards

This is the part that must match on both sides. Everything else is an
implementation detail of one board or the other.

**Transport:** UDP broadcast, port **5005**.

- Defined as `SENSOR_UDP_PORT` in `include/config.h` (screen) and as a local
  constant in `tools/sensor_esp_sender/sensor_esp_sender.ino` (sensor).
- Broadcast rather than unicast so the sensor board never has to be told the
  screen's IP address. The sketch derives the *directed* subnet broadcast from
  its own IP and netmask rather than using `255.255.255.255`, because some APs
  drop the latter.
- Fire-and-forget. The sensor board never waits for a reply, so nothing
  happening on the screen can hold up sampling.

**Packet:**

```json
{"seq":1234,"ms":45678,"d":{"temp_c":248.3,"flow":1.02}}
```

| Field | Meaning |
|---|---|
| `seq` | Incrementing counter from the sensor board. **Load-bearing — see below.** |
| `ms`  | `millis()` on the sensor board. Its own sample clock. |
| `d`   | One entry per channel. Names are free-form and discovered by the screen. |

**Do not remove `seq`.** UDP drops packets silently and reorders them. The
sequence number is the only thing that lets the screen distinguish *a lost
sample* from *a sensor that went quiet*. Without it, gaps in the recorded data
are invisible, which for research use is worse than not recording at all. The
screen counts missed numbers, writes a warning to the event log, and reports the
total when the run closes. A sequence number that goes *backwards* is treated as
the sensor board having restarted, not as several billion lost packets.

**Channel names are discovered at runtime.** Add a key to `d` and the screen
picks it up, shows it, and gives it a CSV column with no change on the screen
side. Constraints:

- Maximum **8 channels** (`SensorLink::MAX_CHANNELS`). Extras are ignored with a
  warning.
- Names truncate at **15 characters**.
- A channel keeps its slot for the whole session once seen, so CSV column order
  stays stable even if a later packet omits it.
- Packets with no `d` object are silently ignored — that is deliberate, so other
  UDP traffic on the port cannot corrupt a run.

---

## 3. What to do

### 3a. Flash the transmitter

`tools/sensor_esp_sender/sensor_esp_sender.ino` goes on the **sensor board**,
not the screen. Before flashing:

1. Set `WIFI_SSID` / `WIFI_PASS` to the rig's network — the same one the screen
   joins.
2. Replace the two placeholder functions with the real reads:
   ```cpp
   static float readHotplateTemperature() { return 25.0f; }  // TODO
   static float readFlow()                { return 0.0f;  }  // TODO
   ```
   and adjust the keys in `readSensors()` to whatever you are actually
   measuring. `temp_c` and `flow` are guesses — nobody confirmed the channel
   list, so treat them as examples.
3. Consider `SAMPLE_INTERVAL_MS` (default 200 ms = 5 Hz). Every packet becomes
   one CSV row, so this sets the resolution of the recorded data. 5–10 Hz is
   ample for a hotplate; much faster mostly fills the card.

**ArduinoJson v7 required.** The repo pins `bblanchon/ArduinoJson@7.4.3` and the
sketch uses the v7 API (`JsonDocument`, `doc["d"].to<JsonObject>()`). If your
Arduino IDE has v6 installed it will not compile — v6 used
`StaticJsonDocument`/`createNestedObject`. Install v7.

### 3b. Put an SD card in the screen

Recording writes to the **display's** SD card, not FluidNC's. Without a card you
get a logged error (`No SD card - this run will not be recorded`) rather than a
silently unrecorded run — but you also get no data.

Files land in `/logs/run_NNNN.csv`, numbered from a counter in NVS. There is no
RTC on this board, so files cannot be named by date and `t_ms` is milliseconds
since the recording started, not wall-clock time. If wall-clock timestamps
matter for the research, that needs NTP adding — see Open questions.

---

## 4. Verify, in this order

Do these in sequence. Each one isolates a different failure, and skipping ahead
makes a failure much harder to place.

### Step 1 — the screen is listening

Flash the screen, connect it to a machine (the sensor socket cannot open before
that — see the gotcha below), and watch the serial monitor at 115200:

```
[SensorLink] Listening on UDP 5005 as 192.168.1.42
```

Note that IP. If this line never appears, the screen has not joined WiFi yet.

### Step 2 — the screen receives, without involving the sensor board

**Do this before touching the sensor board.** It separates "the screen can
receive" from "the sensor board can send", which are two different problems.

From a laptop on the same network:

```bash
# Unicast, to the IP from step 1
echo -n '{"seq":1,"ms":100,"d":{"temp_c":25.5,"flow":1.0}}' \
  | nc -u -w1 192.168.1.42 5005

# Then again with seq 2, 3 ... to confirm the counter tracks
```

Expected on the screen's **Log** tab:
- `Sensor channel found: temp_c` and `Sensor channel found: flow`
- Two tiles appear showing `25.50` and `1.00`
- Status line changes to `Sensors live - recording starts with the next run`

Values grey out about 3 seconds after the last packet
(`SensorLink::STALE_AFTER_MS`). That is intended — a number that stopped
updating must not read as a measurement still being taken.

If this fails, the problem is on the screen and the sensor board is irrelevant.

### Step 3 — broadcast reaches the screen

Same as step 2 but to the subnet broadcast address, e.g. `192.168.1.255`. **This
is the step most likely to fail**, and it fails at the access point, not in
either firmware. See Failure modes.

### Step 4 — the real sensor board

Power it up and watch its serial:

```
[Sensor] 192.168.1.57, broadcasting to port 5005
```

Then confirm the screen's tiles track real readings — heat the plate and watch
the number move.

### Step 5 — a recorded run

Press Start on the screen with a program selected. The Log tab status line
should become:

```
💾 /logs/run_0001.csv   412 rows   0 lost
```

Let it run, stop it, power down, pull the card. You should have:

```
# FluidTouch run 1
# program,spray_A.gcode
# speed_percent,120
# duration_limit_s,300
# sensor_channels,2
# t_ms is milliseconds since this recording started
t_ms,seq,temp_c,flow,X,Y,Z,A,state,valve,event
0,1,248.3,1.02,0.000,0.000,30.000,0.000,Run,1,
200,2,248.1,1.03,12.500,0.000,30.000,0.400,Run,1,
340,2,248.1,1.03,18.200,0.000,30.000,0.500,Run,1,"Spray valve closed"
```

Check specifically that **`0 lost`** held for the whole run and that the `X`/`Y`
columns actually move. Both are easy to get wrong and quiet about it.

---

## 5. Gotchas that will cost you an afternoon

**The socket cannot open until a machine is selected.** The screen only calls
`WiFi.begin()` when connecting to a FluidNC machine
(`src/ui/ui_common.cpp:246`). Until the operator has picked one, there is no
network and `SensorLink::init()` keeps failing quietly and retrying from
`loop()`. On a fresh boot sitting at the machine-select screen, no sensor data
will ever arrive. This is expected, not a bug.

**WiFi gets switched off deliberately.** `WiFi.mode(WIFI_OFF)` runs on
disconnect (`ui_common.cpp:163`) and before deep sleep
(`power_manager.cpp:262`). `SensorLink::loop()` detects this, closes the socket,
and re-binds when WiFi returns. If you change the power management, re-test the
sensor link after a sleep/wake cycle.

**Recording only runs during a job.** The tiles are live whenever packets
arrive, but the CSV opens on Start and closes when the job ends. There is
currently no way to record without running a program. If you need standalone
logging, that is a small change to `DataRecorder` — say so rather than working
around it.

**A pendant reboot ends the file.** The screen is the only recorder; the sensor
board keeps no copy. A crash or power blip mid-run loses the remainder. The card
is committed once a second, so you lose about a second, not the whole file.

**`docs/ui-guide.md` is stale.** It documents the pre-fork upstream UI —
Status/Control/Files/Macros/Terminal tabs, none of which exist any more. Do not
use it as a reference for anything.

---

## 6. Failure modes, most likely first

| Symptom | Cause | Fix |
|---|---|---|
| Step 2 works, step 3 doesn't | The AP drops broadcast, or has client isolation on | Switch the sketch to unicast: replace the derived `bcast` with the screen's fixed IP, and give the screen a DHCP reservation |
| Nothing at all, no serial line on the screen | No machine selected yet, so no WiFi | Connect to a machine first |
| Sensor board serial looks fine, screen sees nothing | Different subnets — sensor on a guest network or a second AP | Put both on the same SSID and subnet |
| Packets arrive, no channels appear | `d` object missing or misspelled | Packets without `d` are dropped by design; check the JSON |
| Sketch won't compile | ArduinoJson v6 installed | Install v7 — v6's `StaticJsonDocument` API is incompatible |
| Tiles show values then grey out | Packets stopped for >3 s | Check the sensor board's WiFi reconnect path |
| `N lost` climbing steadily | Genuine packet loss, or the sample rate is too high for the link | Lower `SAMPLE_INTERVAL_MS`; check WiFi signal at the rig |
| Values look right on screen, CSV columns empty | Channel first seen *after* recording started | Columns are frozen when the file header is written; make sure the sensor board is up before pressing Start |

---

## 7. Where the code is

| File | What it does |
|---|---|
| `include/network/sensor_link.h`, `src/network/sensor_link.cpp` | UDP receive, JSON parse, channel discovery, sequence-gap detection |
| `include/ui/data_recorder.h`, `src/ui/data_recorder.cpp` | CSV file per run on the display's SD card |
| `include/ui/event_log.h`, `src/ui/event_log.cpp` | RAM ring of machine messages and operator actions; feeds the CSV's `event` column via a sink |
| `src/ui/tabs/ui_tab_log.cpp` | The Log tab — sensor tiles, recording status, event lines |
| `tools/sensor_esp_sender/sensor_esp_sender.ino` | **The transmitter. Flash to the sensor board.** |
| `include/config.h` | `SENSOR_UDP_PORT`, `RUN_LOG_DIR` |

Wiring points: `SensorLink::loop()` is pumped from `src/main.cpp` alongside
`FluidNCClient::loop()`, and each fresh packet triggers one `DataRecorder::sample()`
row. `DataRecorder::start()`/`stop()` are called from `UITabRun::startJob()` and
`UITabRun::endJob()`.

Build both variants before you commit anything:

```bash
pio run -e elecrow-crowpanel-7-basic
pio run -e elecrow-crowpanel-7-advance-v13
```

---

## 8. Open questions nobody has answered yet

1. **What does the sensor board actually measure?** The channel list was never
   confirmed. `temp_c` and `flow` are placeholders. The screen is
   channel-agnostic so this does not block anything, but the sketch needs real
   reads.
2. **Wall-clock timestamps.** There is no RTC. Everything is relative to the
   start of the recording. If the research needs absolute time, NTP over the
   existing WiFi is the obvious route — roughly a `configTime()` call plus a
   header line in the CSV — but it assumes the rig's network has internet
   access, which nobody has confirmed.
3. **Should the sensor board keep its own copy?** Currently the screen is a
   single point of failure for the data. A local SD or flash ring on the sensor
   board would be the fallback.
4. **Recording outside a run.** See gotchas.

---

## 9. Unrelated things also unflashed on this branch

The `speed` branch carries other untested work that will be flashed at the same
time. The rig convention is **one change per flash, confirmed on hardware before
the next** — do not batch these.

- Move tab rebuilt: one row per axis, tap to jog 1 mm, hold to keep moving
  (release sends jog-cancel `0x85` to flush the queue)
- Stop now genuinely stops — it used to send a door hold (`0x84`), which merely
  paused, making Stop and Pause identical
- Speed (feed-override percentage) and duration (a pendant-side countdown) on
  the home screen

Suggested flash order: naming/labels → Stop behaviour → speed → duration →
sensor link. The first thing to check when testing speed is whether the sweep
moves in the G-code are `G1` and not `G0` — feed override does nothing to `G0`
rapids, and that would look like a firmware bug when it is a G-code one.
