#pragma once
#include <Arduino.h>

// ok=false means the value is invalid (absent sensor, fault, or NaN read).
struct SensorReadings
{
  float tcTempC = NAN; // MAX31856 thermocouple (substrate)
  bool tcOk = false;

  float rtdTempC = NAN; // MAX31865 + PT100 RTD (metal)
  bool rtdOk = false;

  float ambientTempC = NAN; // SHT45
  float ambientRH = NAN;
  bool shtOk = false;

  int32_t vocIndex = -1; // SGP40
  bool sgpOk = false;

  float pressureHPa = NAN; // MPRLS
  bool mprlsOk = false;
};

void sensorsInit();
void sensorsRead(SensorReadings &r);
