#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "config.h"
#include "sensors.h"

static WiFiUDP udp;

// B2: The CSV row is built into this buffer (instead of printed piecewise)
// so the same line can go to both Serial and the UDP broadcast packet.
static char csvLine[128];
static size_t csvLen;

// Appends 1 CSV field, always preceded by a comma. Either appends the value, or
// leaves the field empty when the sensor is unavailable. Overloaded for the
// float channels and the SGP40's integer VOC index.
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

// The UDP payload follows the FluidTouch sensor-link contract:
//   {"seq":N,"ms":M,"d":{"tc_c":351.42,...}}
// seq is how the screen tells a lost packet from a quiet sensor; ms is our
// millis(). A channel whose sensor is unavailable this tick is omitted from
// "d" (the screen keeps its column slot and leaves the row blank).
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

static void tick() // Reads every sensor, then ships the row (serial CSV + UDP JSON)
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

  // seq counts every sample taken, not every packet sent, so ticks that
  // couldn't be sent (WiFi down) show up in the screen's lost-packet count
  // instead of looking like the sensor board went quiet.
  static uint32_t seq = 0;
  seq++;

  // One packet per tick to the subnet broadcast address; the screen just
  // listens on UDP_PORT. Broadcast frames aren't retried at the WiFi layer,
  // so the odd packet drops; the screen's seq accounting surfaces that.
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
  sensorsInit(); // I2C bus up + probe SHT45 / SGP40 / MPRLS (prints [INIT] lines)

  // Join the (offline) rig router. Non-blocking: the tick loop starts
  // immediately and simply skips the UDP send until the connection is up.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void loop()
{
  // Repeats instead of printing once: on native USB-CDC boards like the
  // S3, every reset re-enumerates the port, and a monitor client takes
  // longer to reconnect than a one-shot boot print takes to fire. Repeating
  // means whenever the client finishes reattaching, the next line is at
  // most ~1s away instead of already gone.
  static unsigned long lastMs = 0;
  if (millis() - lastMs >= 1000)
  {
    lastMs = millis();
    Serial.println("[INIT] sensormcu boot (env=esp32s3)");
  }

  // Announce WiFi transitions once per change (the ESP32 core auto-reconnects
  // after drops, so this can fire more than once per boot).
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

  // B1: Scheduler. Single tick driven off millis(); reads every sensor + CSV
  static unsigned long lastTickMs = 0;
  if (millis() - lastTickMs >= LOG_PERIOD_MS)
  {
    lastTickMs = millis();
    tick();
  }

  // TODO(D6): serial commands (STATUS / RESET / TEST)
}
