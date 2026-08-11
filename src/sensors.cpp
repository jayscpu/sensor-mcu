//   I2C: SHT45, SGP40, MPRLS
//   SPI: MAX31856 thermocouple - substrate temp, logging only
//        MAX31865 RTD          - metal temp, SAFETY-CRITICAL

#include "sensors.h"
#include "config.h"

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_SGP40.h>
#include <Adafruit_MPRLS.h>
#include <Adafruit_MAX31856.h>
#include <Adafruit_MAX31865.h>

static Adafruit_SHT4x sht45;
static Adafruit_SGP40 sgp40;
static Adafruit_MPRLS mprls = Adafruit_MPRLS(-1, -1);

// Both temp chips share the hardware SPI bus; each has its own CS pin (config.h).
static Adafruit_MAX31856 maxTC(PIN_MAX31856_CS);
static Adafruit_MAX31865 maxRTD(PIN_MAX31865_CS);

static bool shtPresent = false;
static bool sgpPresent = false;
static bool mprlsPresent = false;
static bool tcPresent = false;
static bool rtdPresent = false;

void sensorsInit()
{
#if SIMULATE_SENSORS
  // Loud and repeated in the probe output so a fake-data build can't be
  // mistaken for a real run.
  Serial.println("[INIT] SIMULATE_SENSORS=1: readings below are FAKE");
  randomSeed(esp_random());
#endif

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  shtPresent = sht45.begin(&Wire);
  if (shtPresent)
  {
    sht45.setPrecision(SHT4X_HIGH_PRECISION);
    sht45.setHeater(SHT4X_NO_HEATER);
    Serial.println("[INIT] SHT45 detected");
  }
  else
  {
    Serial.println("[INIT] SHT45 NOT found");
  }

  sgpPresent = sgp40.begin(&Wire);
  Serial.println(sgpPresent ? "[INIT] SGP40 detected" : "[INIT] SGP40 NOT found");

  mprlsPresent = mprls.begin(MPRLS_DEFAULT_ADDR, &Wire);
  Serial.println(mprlsPresent ? "[INIT] MPRLS detected" : "[INIT] MPRLS NOT found");

  // SPI has no bus-level ack, so begin() returning true only
  // means the driver initialised; a truly absent/dead sensor is caught later
  // as a fault or NaN on read.
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

  tcPresent = maxTC.begin();
  if (tcPresent)
  {
    maxTC.setThermocoupleType(TC_TYPE);
    // Measure continuously in the background so each read is instant (a one-shot
    // read would block ~200 ms). This also keeps the fault flags fresh every tick.
    maxTC.setConversionMode(MAX31856_CONTINUOUS);
    Serial.println("[INIT] MAX31856 detected");
  }
  else
  {
    Serial.println("[INIT] MAX31856 NOT found");
  }

  rtdPresent = maxRTD.begin(RTD_WIRES);
  Serial.println(rtdPresent ? "[INIT] MAX31865 detected" : "[INIT] MAX31865 NOT found");
}

#if SIMULATE_SENSORS
// Random walk: nudge v by up to +/-step per tick, clamped to [lo, hi], so the
// fake channels drift like real signals instead of jumping around.
static float walk(float v, float lo, float hi, float step)
{
  v += step * (random(-100, 101) / 100.0f);
  return constrain(v, lo, hi);
}

void sensorsRead(SensorReadings &r)
{
  static float tc = 350, rtd = 380, amb = 25, rh = 40, press = 1013;

  r.tcTempC = tc = walk(tc, 250, 450, 3.0f);
  r.tcOk = true;
  r.rtdTempC = rtd = walk(rtd, 250, 450, 3.0f);
  r.rtdOk = true;
  r.ambientRH = rh = walk(rh, 20, 70, 0.5f);
  r.ambientTempC = amb = walk(amb, 18, 35, 0.2f);
  r.vocIndex = random(80, 140);
  r.sgpOk = true;
  r.pressureHPa = press = walk(press, 950, 1080, 1.0f);
  r.mprlsOk = true;

  // Roughly 1 tick in 15, pretend the SHT45 read failed, so the receiver's
  // empty-field handling gets exercised too.
  r.shtOk = (random(0, 15) != 0);
  if (!r.shtOk)
  {
    r.ambientTempC = NAN;
    r.ambientRH = NAN;
  }
}
#else
// Single tick (LOG_PERIOD_MS, 1 Hz): all sensors. Keep it at 1 Hz; the
// SGP40 VOC algorithm expects 1 Hz sampling.
void sensorsRead(SensorReadings &r)
{
  // SHT45 first, since the SGP40 compensation below reads its result this same tick.
  if (shtPresent)
  {
    sensors_event_t humidity, temp;
    if (sht45.getEvent(&humidity, &temp))
    {
      r.ambientTempC = temp.temperature;
      r.ambientRH = humidity.relative_humidity;
      r.shtOk = true;
    }
    else
    {
      r.ambientTempC = NAN;
      r.ambientRH = NAN;
      r.shtOk = false;
    }
  }

  // SGP40 VOC index: humidity-compensated from the SHT45 reading when we have
  // one this tick, otherwise the library's defaults.
  if (sgpPresent)
  {
    if (r.shtOk)
    {
      r.vocIndex = sgp40.measureVocIndex(r.ambientTempC, r.ambientRH);
    }
    else
    {
      r.vocIndex = sgp40.measureVocIndex();
    }
    r.sgpOk = true;
  }

  // MPRLS: line pressure in hPa. A NaN read voids the reading.
  if (mprlsPresent)
  {
    float p = mprls.readPressure(); // hPa
    if (isnan(p))
    {
      r.pressureHPa = NAN;
      r.mprlsOk = false;
    }
    else
    {
      r.pressureHPa = p;
      r.mprlsOk = true;
    }
  }

  // MAX31856 thermocouple: sacrificial substrate temp between passes (cooldown
  // tuning; logging). Continuous mode: this just reads the
  // latest conversion, no blocking. A non-zero fault (open circuit, out of
  // range, over/under-voltage) or NaN marks the reading invalid.
  if (tcPresent)
  {
    float t = maxTC.readThermocoupleTemperature(); // deg C
    uint8_t fault = maxTC.readFault();
    if (fault || isnan(t))
    {
      r.tcTempC = NAN;
      r.tcOk = false;
    }
    else
    {
      r.tcTempC = t;
      r.tcOk = true;
    }
  }

  // RTD: safety-critical metal temp. One read ~75 ms, fits the 1 s tick. Its
  // fault flag is sticky (so a fault between reads can't be missed); clear it
  // after reading to re-arm for next tick.
  if (rtdPresent)
  {
    float t = maxRTD.temperature(RTD_RNOMINAL, RTD_RREF); // deg C
    uint8_t fault = maxRTD.readFault();
    if (fault)
    {
      maxRTD.clearFault();
    }
    if (fault || isnan(t))
    {
      r.rtdTempC = NAN;
      r.rtdOk = false;
    }
    else
    {
      r.rtdTempC = t;
      r.rtdOk = true;
    }
  }
}
#endif // SIMULATE_SENSORS
