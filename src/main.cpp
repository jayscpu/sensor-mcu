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

// TEMP(B1): tick counters to prove cadence on the serial monitor. Removed in
// B2, once the 1 Hz DATA line itself is the proof the slow tick fires.
static unsigned long fastCount = 0;
static unsigned long slowCount = 0;
static void fastTick()
{
  sensorsReadFast(g_readings);
  fastCount++;
  // TODO(D2+): safetyEvaluate(g_readings) on the fast tick.
}

// Slow tick: logging sensors (SHT45, SGP40, ADS1115), every LOG_PERIOD_MS
// (1 Hz: the SGP40 VOC algorithm expects that cadence).
static void slowTick()
{
  sensorsReadSlow(g_readings);
  slowCount++;
  // TEMP(B1): proves both ticks fire; fast should advance ~4x per slow tick
  // (SAFETY_POLL_MS 250 vs LOG_PERIOD_MS 1000). Replaced by the DATA line in
  // B2.
  Serial.printf("[STATUS] sched fast=%lu slow=%lu\n", fastCount, slowCount);
  // TODO(B2): emit the 1 Hz CSV DATA line from g_readings here.
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

  // B1: non-blocking scheduler. Two independent ticks driven off millis(),
  // same repeat-don't-gate idiom as the banner/heartbeat above. Subtraction is
  // wrap-safe across the ~49.7-day millis() rollover; resetting lastX from the
  // actual fire time means a late loop skips a beat instead of bursting to
  // catch up (never desirable on the safety tick). No delay() anywhere.
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

  // TODO(B2): 1 Hz CSV DATA line from SensorReadings (in slowTick)
  // TODO(D2+): safetyEvaluate() on the fast tick
  // TODO(D6): serial commands (STATUS / RESET / TEST)
}
