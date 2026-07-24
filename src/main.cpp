#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include "sensors.h"
#include "safety.h"

static Adafruit_NeoPixel statusLed(1, PIN_STATUS_LED, NEO_RGB + NEO_KHZ800);

// B1: shared snapshot both ticks write into. Persistent (file scope) so the
// fast tick's fields and the slow tick's fields accumulate into one coherent
// row that B2 will read to build the DATA line.
static SensorReadings g_readings;

static void fastTick()
{
  sensorsReadFast(g_readings);
  // TODO(D2+): safetyEvaluate(g_readings) on the fast tick.
}

// B2: Prints 1 CSV field, always preceded by a comma. Either prints the value, or
// leaves the field empty when the sensor is unavailable. Overloaded for the
// float channels and the SGP40's integer VOC index.
static void csvField(bool ok, float value, uint8_t decimals)
{
  Serial.print(',');
  if (ok)
    Serial.print(value, decimals);
}
static void csvField(bool ok, int32_t value)
{
  Serial.print(',');
  if (ok)
    Serial.print(value);
}

static void slowTick() // Prints the CSV row, which pulls the fast tick's fields in from the shared snapshot
{
  sensorsReadSlow(g_readings);
  Serial.print("DATA");
  csvField(g_readings.tcOk, g_readings.tcTempC, 2);
  csvField(g_readings.rtdOk, g_readings.rtdTempC, 2);
  csvField(g_readings.shtOk, g_readings.ambientTempC, 2);
  csvField(g_readings.shtOk, g_readings.ambientRH, 1);
  csvField(g_readings.sgpOk, g_readings.vocIndex);
  csvField(g_readings.mprlsOk, g_readings.pressureHPa, 1);
  Serial.println();
}

void setup()
{
  Serial.begin(115200);
  statusLed.begin();
  statusLed.setBrightness(64);
  sensorsInit(); // I2C bus up + probe SHT45 / SGP40 / MPRLS (prints [INIT] lines)
  safetyInit();  // TODO(D1): no-op until pin drive is implemented; keeps setup() intent complete
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

  // 1 Hz heartbeat: proves loop() is alive at a glance, independent of the
  // serial connection. No delay(); this is what A2's checklist is checking.
  // Green stands in for "OK" until D5 wires this to real safety state.
  static unsigned long lastBlinkMs = 0;
  static bool ledOn = false;
  if (millis() - lastBlinkMs >= 500)
  {
    lastBlinkMs = millis();
    ledOn = !ledOn;
    statusLed.setPixelColor(0, ledOn ? statusLed.Color(0, 255, 0) : 0);
    statusLed.show();
  }

  // B1: Scheduler. Two independent ticks driven off millis()
  static unsigned long lastFastMs = 0;
  if (millis() - lastFastMs >= SAFETY_POLL_MS)
  { // fast: safety-critical reads
    lastFastMs = millis();
    fastTick();
  }
  static unsigned long lastSlowMs = 0;
  if (millis() - lastSlowMs >= LOG_PERIOD_MS)
  { // slow: logging reads + CSV
    lastSlowMs = millis();
    slowTick();
  }

  // TODO(D2+): safetyEvaluate() on the fast tick
  // TODO(D6): serial commands (STATUS / RESET / TEST)
}
