#ifndef GREENHOUSE_SIMPLE_SOIL_SENSOR_H
#define GREENHOUSE_SIMPLE_SOIL_SENSOR_H

#include <stdint.h>

// These are ADC measurements, not calibrated soil-moisture percentages.
struct SoilReading {
  bool valid = false;
  int raw = 0;
  uint16_t millivolts = 0;
  uint32_t sampledAt = 0;

  bool fresh(uint32_t now) const {
    return valid && uint32_t(now - sampledAt) <= 2000U;
  }
};

#endif
