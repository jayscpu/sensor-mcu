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
  randomSeed(esp_random());
#endif

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  shtPresent = sht45.begin(&Wire);
  if (shtPresent)
  {
    sht45.setPrecision(SHT4X_HIGH_PRECISION);
    sht45.setHeater(SHT4X_NO_HEATER);
  }

  sgpPresent = sgp40.begin(&Wire);
  mprlsPresent = mprls.begin(MPRLS_DEFAULT_ADDR, &Wire);

  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

  tcPresent = maxTC.begin();
  if (tcPresent)
  {
    maxTC.setThermocoupleType(TC_TYPE);
    // Continuous mode: a one-shot read blocks ~200 ms
    maxTC.setConversionMode(MAX31856_CONTINUOUS);
  }

  rtdPresent = maxRTD.begin(RTD_WIRES);
}

#if SIMULATE_SENSORS

// Random walk so fake channels drift like real signals
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

  // Occasional fake SHT45 dropouts
  r.shtOk = (random(0, 15) != 0);
  if (!r.shtOk)
  {
    r.ambientTempC = NAN;
    r.ambientRH = NAN;
  }
}

#else

void sensorsRead(SensorReadings &r)
{
  // SHT45 before SGP40: the VOC compensation needs it
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

  if (mprlsPresent)
  {
    float p = mprls.readPressure();
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

  if (tcPresent)
  {
    float t = maxTC.readThermocoupleTemperature();
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

  // Note: the MAX31865 fault flag is sticky
  if (rtdPresent)
  {
    float t = maxRTD.temperature(RTD_RNOMINAL, RTD_RREF);
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
