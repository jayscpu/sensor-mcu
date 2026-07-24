#pragma once
#include "sensors.h"

enum class SafetyState { OK, WARN, SHUTDOWN };

void safetyInit();                                     // TODO: cutoff/warn pins to OK level
SafetyState safetyEvaluate(const SensorReadings &r);   // TODO: thresholds, latching, pin drive
SafetyState safetyState();
