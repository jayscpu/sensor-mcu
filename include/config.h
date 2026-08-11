#pragma once

// =============================================================
// Pin map: ESP32-S3 DevKitC-1 (the only board this project targets).
// =============================================================

// I2C bus: SHT45, SGP40, MPRLS
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9

// SPI bus: MAX31856 + MAX31865
#define PIN_SPI_SCK  12
#define PIN_SPI_MISO 13
#define PIN_SPI_MOSI 11
#define PIN_MAX31856_CS 10
#define PIN_MAX31865_CS 14

// Outputs to the FluidNC controller (common GND required).
// Fail-safe polarity: LOW = OK, HIGH/floating = fault. Configure the
// FluidNC input as active-high with a pull-up (see README.md) so a broken
// wire or a dead sensor MCU also reads as a fault.
#define PIN_SHUTDOWN 4
#define PIN_WARN     5

#define CUTOFF_OK_LEVEL    LOW
#define CUTOFF_FAULT_LEVEL HIGH

// =============================================================
// Sensor configuration
// =============================================================

// 1 = ignore real sensors and generate plausible wandering fake readings each
// tick, so the UDP/CrowPanel pipeline can be tested with no sensors wired.
// The CSV format is identical to real data. SET BACK TO 0 for real runs.
#define SIMULATE_SENSORS 1

// MAX31856 thermocouple type (sacrificial substrate temperature, for cooldown tuning)
#define TC_TYPE MAX31856_TCTYPE_K

// MAX31865 RTD: PT100 with the Adafruit breakout's 430 ohm reference.
// Set RTD_WIRES to MAX31865_2WIRE / MAX31865_3WIRE / MAX31865_4WIRE
// to match how the PT100 is wired.
#define RTD_WIRES    MAX31865_3WIRE
#define RTD_RREF     430.0f
#define RTD_RNOMINAL 100.0f

// ADS1115 spare analog channels. Driver is not currently built (no analog
// inputs wired yet); this setting is kept for when it's re-added. Full-range
// +/-6.144V (GAIN_TWOTHIRDS) is the safe default: it won't clip a 0-3.3V or
// 0-5V source. Narrow it (e.g. GAIN_ONE = +/-4.096V) for more resolution once
// the signal range is known.
#define ADS_GAIN GAIN_TWOTHIRDS

// =============================================================
// WiFi / UDP telemetry
// =============================================================

// Closed rig network: the old router is offline, so credentials here are
// low-sensitivity, but still fill in your own. Each tick, one JSON packet
// (FluidTouch sensor-link contract, see README) goes to the subnet broadcast
// address; the CrowPanel screen listens on UDP_PORT. No fixed IPs.
#define WIFI_SSID "sprayer"
#define WIFI_PASS "sprayer123"
// Must match SENSOR_UDP_PORT in the FluidTouch repo's include/config.h.
#define UDP_PORT  5005

// =============================================================
// Timing
// =============================================================

#define LOG_PERIOD_MS 1000 // sensor tick: all reads + CSV log line
                           // (keep at 1000 ms: the SGP40 VOC algorithm expects 1 Hz)

// =============================================================
// Safety thresholds: TUNE THESE for your process before trusting them.
// WARN asserts the warning pin (non-latching); SHUTDOWN asserts the
// cutoff pin and latches until a RESET serial command or power cycle.
// =============================================================

// Thermocouple = sacrificial substrate temp (cooldown tuning), deg C.
// NOT a safety cutoff despite the _SHUTDOWN_ name; a logging/warning
// reference only. Only the RTD (and pressure, if enabled) drive the pin.
#define TC_WARN_C     450.0f
#define TC_SHUTDOWN_C 500.0f

// PT100 RTD temperature, deg C
#define RTD_WARN_C     450.0f
#define RTD_SHUTDOWN_C 500.0f

// MPRLS line pressure window, hPa. Outside the window raises a warning.
// Set ENABLE_PRESSURE_TRIP to 1 to make it a latching shutdown instead.
#define PRESS_MIN_HPA 800.0f
#define PRESS_MAX_HPA 1600.0f
#define ENABLE_PRESSURE_TRIP 0

// SGP40 VOC index (1..500, ~100 is typical clean air). Warning only.
#define VOC_WARN_INDEX 300

// Consecutive failed reads of a safety-critical sensor (TC or RTD)
// before we treat the sensor itself as failed and trip a shutdown.
// 8 reads at the 1 s tick = 8 s of no valid data.
#define SENSOR_FAULT_TRIP_COUNT 8
