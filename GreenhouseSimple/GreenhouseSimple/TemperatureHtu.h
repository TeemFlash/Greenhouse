#ifndef GREENHOUSE_SIMPLE_TEMPERATURE_HTU_H
#define GREENHOUSE_SIMPLE_TEMPERATURE_HTU_H

#include <math.h>
#include <Wire.h>
#include "TemperatureSensor.h"

// HTU21D(F), 0x40: ambient AIR temperature only; no humidity/soil calculation.
// Protocol: TE Connectivity HTU21D(F) datasheet, command, CRC and conversion
// sections: https://www.te.com/en/product-CAT-HSC0004.html
// Call tick() in the main loop with the display DC pin released for I2C.
// Only command 0xF3 (no-hold temperature) is written; no reset, heater, user
// register, PCF8575 output or calibration writes are performed.
class TemperatureHtu {
 public:
  // A pending conversion satisfies the request. Fault retries remain limited
  // to once per 1500 ms even if the user repeatedly requests an update.
  void request() { requested_ = true; }

  // Returns true for each new measurement or a visible loss of valid data.
  bool tick(uint32_t now, TemperatureReading& reading) {
    bool changed = false;
    if (reading.valid && !reading.fresh(now)) {
      reading.valid = false;
      changed = true;
    }

    if (!converting_) {
      const bool due = !attempted_ || uint32_t(now - cycleAt_) >= kPeriodMs;
      if (!due && !(requested_ && !retryBackoff_)) return changed;
      attempted_ = true;
      requested_ = false;
      cycleAt_ = conversionAt_ = now;
      Wire.beginTransmission(kAddress);
      const bool queued = Wire.write(uint8_t(0xF3)) == 1;
      const uint8_t result = Wire.endTransmission();
      if (!queued || result != 0) return fail(now, reading) || changed;
      converting_ = true;
      return changed;
    }

    // The maximum 14-bit temperature conversion is 50 ms. Waiting 60 ms
    // without delay() permits display, buttons and ADS1115 work in between.
    if (uint32_t(now - conversionAt_) < kConversionMs) return changed;
    if (Wire.requestFrom(kAddress, uint8_t(3)) != 3 || Wire.available() < 3) {
      return fail(now, reading) || changed;
    }
    const int high = Wire.read();
    const int low = Wire.read();
    const int checksum = Wire.read();
    if (high < 0 || high > 255 || low < 0 || low > 255 ||
        checksum < 0 || checksum > 255) {
      return fail(now, reading) || changed;
    }

    // CRC covers the original bytes, including status bits.
    if (crc8(uint8_t(high), uint8_t(low)) != uint8_t(checksum)) {
      return fail(now, reading) || changed;
    }
    const uint16_t received = uint16_t((uint16_t(high) << 8) | uint16_t(low));
    // Temperature data has status 00. Reject humidity (10), diagnostics (11)
    // and the unassigned status bit before masking either bit away.
    if ((received & 3U) != 0) return fail(now, reading) || changed;
    const uint16_t raw = received & 0xFFFCU;
    const float celsius = -46.85f + 175.72f * (float(raw) / 65536.0f);
    if (!isfinite(celsius) || celsius < -40.0f || celsius > 125.0f) {
      return fail(now, reading) || changed;
    }

    reading.celsius = celsius;
    reading.sampledAt = now;
    reading.valid = true;
    converting_ = false;
    retryBackoff_ = false;
    requested_ = false;
    return true;
  }

 private:
  static constexpr uint8_t kAddress = 0x40;
  static constexpr uint32_t kPeriodMs = 1500;
  static constexpr uint32_t kConversionMs = 60;
  bool converting_ = false;
  bool attempted_ = false;
  bool requested_ = true;
  bool retryBackoff_ = false;
  uint32_t cycleAt_ = 0;
  uint32_t conversionAt_ = 0;

  bool fail(uint32_t now, TemperatureReading& reading) {
    // A transient bus/CRC fault must not erase a still-fresh checked sample.
    // Keep its original timestamp: failures never extend the 5-second limit.
    const bool wasValid = reading.valid;
    reading.valid = reading.fresh(now);
    converting_ = false;
    retryBackoff_ = true;
    requested_ = false;
    cycleAt_ = now;
    return wasValid != reading.valid;
  }

  static uint8_t crc8(uint8_t high, uint8_t low) {
    uint8_t crc = 0;
    const uint8_t data[] = {high, low};
    for (uint8_t byte : data) {
      crc ^= byte;
      for (unsigned bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x80U) ? uint8_t((crc << 1) ^ 0x31U) : uint8_t(crc << 1);
      }
    }
    return crc;
  }
};

#endif
