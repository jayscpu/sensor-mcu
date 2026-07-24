#pragma once
#include <Arduino.h>

// Provisional data schema shared by the sensors, safety, and logging
// subsystems. Extend/change it as sensors get implemented.
struct SensorReadings {
  // MAX31856 thermocouple (hotplate)
  float   tcTempC = NAN;
  bool    tcOk = false;

  // MAX31865 + PT100 RTD
  float   rtdTempC = NAN;
  bool    rtdOk = false;

  // SHT45 ambient temperature / humidity
  float   ambientTempC = NAN;
  float   ambientRH = NAN;
  bool    shtOk = false;

  // SGP40 VOC index
  int32_t vocIndex = -1;
  bool    sgpOk = false;

  // MPRLS ported pressure
  float   pressureHPa = NAN;
  bool    mprlsOk = false;
};

void sensorsInit();                       // probe + configure every sensor (I2C + SPI)
void sensorsReadFast(SensorReadings &r);  // fast-tick reads: RTD (safety), substrate TC, pressure
void sensorsReadSlow(SensorReadings &r);  // logging reads: SHT45, SGP40
