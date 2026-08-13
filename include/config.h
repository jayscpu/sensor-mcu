#pragma once

// Pin map
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9
#define PIN_SPI_SCK 12
#define PIN_SPI_MISO 13
#define PIN_SPI_MOSI 11
#define PIN_MAX31856_CS 10
#define PIN_MAX31865_CS 14

// Sensors
#define TC_TYPE MAX31856_TCTYPE_K
#define RTD_WIRES MAX31865_3WIRE
#define RTD_RREF 430.0f
#define RTD_RNOMINAL 100.0f

// 1 = broadcast fake wandering readings (no sensors needed). SET TO 0 for real runs
#define SIMULATE_SENSORS 1

// WiFi / UDP telemetry
#define WIFI_SSID "sprayer"
#define WIFI_PASS "sprayer123"

// Destination port. Must match SENSOR_UDP_PORT in touchscreen/include/config.h,
// which the screen listens on (our source port doesn't matter)
#define UDP_PORT 5005

// Bit order of the "status" health bitmask channel; part of the contract with the display
// bit0 SHT45, bit1 SGP40, bit2 MPRLS, bit3 MAX31856, bit4 MAX31865. 31 = all OK

// Keep at 1000 ms: the SGP40 VOC algorithm expects 1 Hz
#define LOG_PERIOD_MS 1000
