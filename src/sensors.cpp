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
    // read would block ~200 ms, too long for the 250 ms tick). This also keeps
    // the fault flags fresh every tick.
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

// Fast tick (SAFETY_POLL_MS, 4 Hz): safety-critical + fast-moving sensors.
void sensorsReadFast(SensorReadings &r)
{
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

  // RTD: safety-critical metal temp. One read ~75 ms, fits the 250 ms tick. Its
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

// Slow tick (LOG_PERIOD_MS, 1 Hz): logging sensors.
void sensorsReadSlow(SensorReadings &r)
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
}
