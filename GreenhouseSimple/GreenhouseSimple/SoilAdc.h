#ifndef GREENHOUSE_SIMPLE_SOIL_ADC_H
#define GREENHOUSE_SIMPLE_SOIL_ADC_H

#include <Wire.h>
#include "SoilSensor.h"

// ADS1115 at 0x48: AIN0 versus GND, +/-4.096 V, 128 SPS, single shot.
// TI ADS1115 datasheet, Config Register: https://www.ti.com/lit/gpn/ads1115
// Call tick() only while the display's DC pin is released for I2C.
// There are no delays, PCF8575 writes, or stored calibration settings here.
class SoilAdc {
 public:
  // Request an earlier sample. An active conversion already satisfies it;
  // repeated requests cannot bypass the 500 ms retry interval after a fault.
  void request() { requested_ = true; }

  // Returns true on every new sample, or when a visible valid value is lost.
  bool tick(uint32_t now, SoilReading& reading) {
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
      cycleAt_ = conversionAt_ = polledAt_ = now;
      if (!writeRegister(kConfigRegister, kConfig)) {
        return fail(now, reading) || changed;
      }
      converting_ = true;
      return changed;
    }

    // A conversion nominally takes 7.8125 ms. Check at most every 10 ms.
    if (uint32_t(now - polledAt_) < kPollMs) return changed;
    polledAt_ = now;

    uint16_t status = 0;
    if (!readRegister(kConfigRegister, status) ||
        (status & 0x7FFFU) != (kConfig & 0x7FFFU)) {
      return fail(now, reading) || changed;
    }
    if (!(status & 0x8000U)) {
      if (uint32_t(now - conversionAt_) >= kTimeoutMs) {
        return fail(now, reading) || changed;
      }
      return changed;
    }

    uint16_t bits = 0;
    if (!readRegister(kConversionRegister, bits)) {
      return fail(now, reading) || changed;
    }
    const int32_t raw = (bits & 0x8000U) ? int32_t(bits) - 65536L : int32_t(bits);
    // The sensor and ADC run from 3.3 V. Do not turn signed/over-range
    // results into plausible-looking measurements.
    if (raw < 0 || raw > 26400) return fail(now, reading) || changed;

    reading.raw = int(raw);
    // At +/-4.096 V one ADS1115 count is 0.125 mV; round to nearest mV.
    reading.millivolts = uint16_t((uint32_t(raw) + 4U) / 8U);
    reading.sampledAt = now;
    reading.valid = true;
    converting_ = false;
    retryBackoff_ = false;
    requested_ = false;
    return true;
  }

 private:
  static constexpr uint8_t kAddress = 0x48;
  static constexpr uint8_t kConversionRegister = 0;
  static constexpr uint8_t kConfigRegister = 1;
  static constexpr uint16_t kConfig = 0xC383;
  static constexpr uint32_t kPeriodMs = 500;
  static constexpr uint32_t kPollMs = 10;
  static constexpr uint32_t kTimeoutMs = 100;

  bool converting_ = false;
  bool attempted_ = false;
  bool requested_ = true;
  bool retryBackoff_ = false;
  uint32_t cycleAt_ = 0;
  uint32_t conversionAt_ = 0;
  uint32_t polledAt_ = 0;

  bool fail(uint32_t now, SoilReading& reading) {
    const bool changed = reading.valid;
    reading.valid = false;
    converting_ = false;
    retryBackoff_ = true;
    requested_ = false;
    cycleAt_ = now;
    return changed;
  }

  static bool writeRegister(uint8_t reg, uint16_t value) {
    Wire.beginTransmission(kAddress);
    const bool pointerQueued = Wire.write(reg) == 1;
    const bool highQueued = Wire.write(uint8_t(value >> 8)) == 1;
    const bool lowQueued = Wire.write(uint8_t(value)) == 1;
    const uint8_t result = Wire.endTransmission();
    return pointerQueued && highQueued && lowQueued && result == 0;
  }

  static bool readRegister(uint8_t reg, uint16_t& value) {
    Wire.beginTransmission(kAddress);
    const bool pointerQueued = Wire.write(reg) == 1;
    const uint8_t result = Wire.endTransmission(false);
    if (!pointerQueued || result != 0) return false;
    if (Wire.requestFrom(kAddress, uint8_t(2)) != 2 || Wire.available() < 2) {
      return false;
    }
    const int high = Wire.read();
    const int low = Wire.read();
    if (high < 0 || high > 255 || low < 0 || low > 255) return false;
    value = uint16_t((uint16_t(high) << 8) | uint16_t(low));
    return true;
  }
};

#endif
