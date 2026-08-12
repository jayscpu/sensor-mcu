#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "config.h"
#include "sensors.h"

static WiFiUDP udp;

static char csvLine[128];
static size_t csvLen;

// Empty CSV field = that sensor is unavailable this tick.
static void csvField(bool ok, float value, uint8_t decimals)
{
  csvLen += snprintf(csvLine + csvLen, sizeof(csvLine) - csvLen,
                     ok ? ",%.*f" : ",", decimals, value);
}
static void csvField(bool ok, int32_t value)
{
  csvLen += snprintf(csvLine + csvLen, sizeof(csvLine) - csvLen,
                     ok ? ",%ld" : ",", (long)value);
}

// FluidTouch sensor-link contract: {"seq":N,"ms":M,"d":{...}}. Channels whose
// sensor is unavailable are omitted from "d".
static char jsonBuf[256];
static size_t jsonLen;
static bool jsonFirst;

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

  csvLen = snprintf(csvLine, sizeof(csvLine), "DATA");
  csvField(readings.tcOk, readings.tcTempC, 2);
  csvField(readings.rtdOk, readings.rtdTempC, 2);
  csvField(readings.shtOk, readings.ambientTempC, 2);
  csvField(readings.shtOk, readings.ambientRH, 1);
  csvField(readings.sgpOk, readings.vocIndex);
  csvField(readings.mprlsOk, readings.pressureHPa, 1);
  Serial.println(csvLine);

  // seq counts samples, not sends, so a WiFi outage registers as lost
  // packets on the screen instead of looking like a quiet sensor.
  static uint32_t seq = 0;
  seq++;

  if (WiFi.status() == WL_CONNECTED)
  {
    jsonLen = snprintf(jsonBuf, sizeof(jsonBuf), "{\"seq\":%lu,\"ms\":%lu,\"d\":{",
                       (unsigned long)seq, (unsigned long)millis());
    jsonFirst = true;
    jsonChannel(readings.tcOk, "tc_c", readings.tcTempC, 2);
    jsonChannel(readings.rtdOk, "rtd_c", readings.rtdTempC, 2);
    jsonChannel(readings.shtOk, "ambient_c", readings.ambientTempC, 2);
    jsonChannel(readings.shtOk, "ambient_rh", readings.ambientRH, 1);
    jsonChannel(readings.sgpOk, "voc_index", readings.vocIndex);
    jsonChannel(readings.mprlsOk, "pressure_hpa", readings.pressureHPa, 1);
    jsonLen += snprintf(jsonBuf + jsonLen, sizeof(jsonBuf) - jsonLen, "}}");

    udp.beginPacket(WiFi.broadcastIP(), UDP_PORT);
    udp.write((const uint8_t *)jsonBuf, jsonLen);
    udp.endPacket();
  }
}

void setup()
{
  Serial.begin(115200);
  sensorsInit();

  // Non-blocking join: ticks run regardless, UDP sends skip until connected.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void loop()
{
  // Repeats because the S3's native USB re-enumerates on every reset; a
  // one-shot boot print would be gone before a monitor can reattach.
  static unsigned long lastMs = 0;
  if (millis() - lastMs >= 1000)
  {
    lastMs = millis();
    Serial.println("[INIT] sensormcu boot (env=esp32s3)");
  }

  static bool wifiWasUp = false;
  bool wifiUp = (WiFi.status() == WL_CONNECTED);
  if (wifiUp != wifiWasUp)
  {
    wifiWasUp = wifiUp;
    if (wifiUp)
    {
      Serial.print("[WIFI] connected, ip=");
      Serial.print(WiFi.localIP());
      Serial.print(", broadcasting sensor JSON to ");
      Serial.print(WiFi.broadcastIP());
      Serial.print(":");
      Serial.println(UDP_PORT);
    }
    else
    {
      Serial.println("[WIFI] disconnected, retrying");
    }
  }

  static unsigned long lastTickMs = 0;
  if (millis() - lastTickMs >= LOG_PERIOD_MS)
  {
    lastTickMs = millis();
    tick();
  }
}
