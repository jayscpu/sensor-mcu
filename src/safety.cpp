// cutoff pin LOW = OK, HIGH/floating = fault (fail-safe polarity).
#include "safety.h"
#include "config.h"

static SafetyState state = SafetyState::OK;

void safetyInit()
{
  // TODO(D1): pinMode + drive PIN_SHUTDOWN / PIN_WARN to CUTOFF_OK_LEVEL.
}

SafetyState safetyEvaluate(const SensorReadings &r)
{
  // TODO(D2-D4): threshold checks, WARN (non-latching), SHUTDOWN (latching),
  // sensor-fault counters, output pin drive.
  (void)r;
  return state;
}

SafetyState safetyState() { return state; }
