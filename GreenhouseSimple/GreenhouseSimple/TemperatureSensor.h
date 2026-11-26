#ifndef GREENHOUSE_SIMPLE_TEMPERATURE_SENSOR_H
#define GREENHOUSE_SIMPLE_TEMPERATURE_SENSOR_H

#include <stdint.h>

// HTU21D(F) measures the air temperature at its package, not soil temperature.
struct TemperatureReading {
  bool valid = false;
  float celsius = 0.0f;
  uint32_t sampledAt = 0;

  bool fresh(uint32_t now) const {
    return valid && uint32_t(now - sampledAt) <= 5000U;
  }
};

#endif
