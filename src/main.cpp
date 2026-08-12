#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "config.h"
#include "sensors.h"

static WiFiUDP udp;

// FluidTouch sensor-link contract: {"seq":N,"ms":M,"d":{...}}. Channels whose
// sensor is unhealthy are omitted from "d"; the "status" channel (bitmask,
// see config.h) says which sensors are healthy and is sent in every packet.
static char jsonBuf[256];
static size_t jsonLen;
static bool jsonFirst;

// Asymmetric debounce: one bad read marks the sensor unhealthy immediately,
// but it must read good for 3 consecutive ticks to count as healthy again.
struct Health
{
  bool healthy = false;
  uint8_t goodStreak = 0;
  bool update(bool ok)
  {
    if (!ok)
    {
      healthy = false;
      goodStreak = 0;
    }
    else if (!healthy && ++goodStreak >= 3)
    {
      healthy = true;
    }
    return healthy;
  }
};

static void jsonChannel(bool ok, const char *name, float value, uint8_t decimals)
{
  if (!ok)
    return;
  jsonLen += snprintf(jsonBuf + jsonLen, sizeof(jsonBuf) - jsonLen,
                      "%s\"%s\":%.*f", jsonFirst ? "" : ",", name, decimals, value);
  jsonFirst = false;
}
static void jsonChannel(bool ok, const char *name, int32_t value)
{
  if (!ok)
    return;
  jsonLen += snprintf(jsonBuf + jsonLen, sizeof(jsonBuf) - jsonLen,
                      "%s\"%s\":%ld", jsonFirst ? "" : ",", name, (long)value);
  jsonFirst = false;
}

static void tick()
{
  SensorReadings readings;
  sensorsRead(readings);

  // seq counts samples, not sends, so a WiFi outage registers as lost
  // packets on the screen instead of looking like a quiet sensor.
  static uint32_t seq = 0;
  seq++;

  static Health shtHealth, sgpHealth, mprlsHealth, tcHealth, rtdHealth;
  bool shtUp = shtHealth.update(readings.shtOk);
  bool sgpUp = sgpHealth.update(readings.sgpOk);
  bool mprlsUp = mprlsHealth.update(readings.mprlsOk);
  bool tcUp = tcHealth.update(readings.tcOk);
  bool rtdUp = rtdHealth.update(readings.rtdOk);
  int32_t status = (shtUp ? 1 : 0) | (sgpUp ? 2 : 0) | (mprlsUp ? 4 : 0) |
                   (tcUp ? 8 : 0) | (rtdUp ? 16 : 0);

  if (WiFi.status() == WL_CONNECTED)
  {
    jsonLen = snprintf(jsonBuf, sizeof(jsonBuf), "{\"seq\":%lu,\"ms\":%lu,\"d\":{",
                       (unsigned long)seq, (unsigned long)millis());
    jsonFirst = true;
    jsonChannel(tcUp, "tc_c", readings.tcTempC, 2);
    jsonChannel(rtdUp, "rtd_c", readings.rtdTempC, 2);
    jsonChannel(shtUp, "ambient_c", readings.ambientTempC, 2);
    jsonChannel(shtUp, "ambient_rh", readings.ambientRH, 1);
    jsonChannel(sgpUp, "voc_index", readings.vocIndex);
    jsonChannel(mprlsUp, "pressure_hpa", readings.pressureHPa, 1);
    jsonChannel(true, "status", status);
    jsonLen += snprintf(jsonBuf + jsonLen, sizeof(jsonBuf) - jsonLen, "}}");

    udp.beginPacket(WiFi.broadcastIP(), UDP_PORT);
    udp.write((const uint8_t *)jsonBuf, jsonLen);
    udp.endPacket();
  }
}

void setup()
{
  sensorsInit();

  // Non-blocking join: ticks run regardless, UDP sends skip until connected.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void loop()
{
  static unsigned long lastTickMs = 0;
  if (millis() - lastTickMs >= LOG_PERIOD_MS)
  {
    lastTickMs = millis();
    tick();
  }
}
