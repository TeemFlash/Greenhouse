#ifndef GREENHOUSE_SIMPLE_SOIL_CALIBRATION_H
#define GREENHOUSE_SIMPLE_SOIL_CALIBRATION_H

#include <stddef.h>
#include <stdint.h>
#include "SoilSensor.h"

// User-captured reference points, never factory/fabricated calibration values.
// dry -> 0%, wet -> 100%; the ADC response may rise or fall with moisture.
// The result is a relative calibrated scale, not volumetric water content.
struct SoilCalibration {
  int dry = 0;
  int wet = 0;
  uint8_t mask = 0;  // Bit 0: dry captured. Bit 1: wet captured.

  static constexpr size_t kStorageSize = 20;
  static constexpr int kMaxRaw = 26400;
  static constexpr int kMinSpan = 500;

  bool valid() const { return mask == 3 && recordValid(); }

  int percent(const SoilReading& reading, uint32_t now) const {
    if (!valid() || !reading.fresh(now)) return -1;
    const int64_t span = int64_t(wet) - dry;
    const int64_t extent = span > 0 ? span : -span;
    const int64_t position = span > 0 ? int64_t(reading.raw) - dry
                                     : int64_t(dry) - reading.raw;
    if (position <= 0) return 0;
    if (position >= extent) return 100;
    return int((position * 100 + extent / 2) / extent);
  }

  // wetPoint=false captures dry; true captures wet. Invalid captures preserve
  // the entire previous state, including a previously valid calibration.
  bool capture(bool wetPoint, int raw) {
    if (!inRange(raw) || (mask & uint8_t(~3U))) return false;
    const uint8_t otherBit = wetPoint ? 1 : 2;
    const int other = wetPoint ? dry : wet;
    if ((mask & otherBit) &&
        (!inRange(other) || distance(raw, other) < kMinSpan)) return false;
    if (wetPoint) wet = raw;
    else dry = raw;
    mask |= wetPoint ? 2 : 1;
    return true;
  }

  void clear() {
    dry = wet = 0;
    mask = 0;
  }

  // Explicit 20-byte record, independent of struct layout and CPU endianness:
  // 0..3 "SCAL"; 4 version=1; 5 mask; 6..7 reserved zero;
  // 8..11 dry, 12..15 wet as unsigned LE32; 16..19 CRC32 of bytes 0..15.
  // CRC is CRC-32/ISO-HDLC (reflected 0xEDB88320, init/xorout 0xFFFFFFFF).
  void encode(uint8_t* out) const {
    out[0] = 'S'; out[1] = 'C'; out[2] = 'A'; out[3] = 'L';
    out[4] = 1;
    out[5] = mask;
    out[6] = out[7] = 0;
    store32(out + 8, uint32_t(dry));
    store32(out + 12, uint32_t(wet));
    store32(out + 16, crc32(out, 16));
  }

  // Loading is transactional. A bad record never destroys current references.
  // Partial captures (mask 0, 1, 2) can be persisted but do not yield percent.
  bool decode(const uint8_t* in) {
    if (in[0] != 'S' || in[1] != 'C' || in[2] != 'A' || in[3] != 'L' ||
        in[4] != 1 || in[6] != 0 || in[7] != 0 ||
        load32(in + 16) != crc32(in, 16)) return false;
    const uint32_t storedDry = load32(in + 8);
    const uint32_t storedWet = load32(in + 12);
    if (storedDry > uint32_t(kMaxRaw) || storedWet > uint32_t(kMaxRaw))
      return false;
    SoilCalibration candidate;
    candidate.dry = int(storedDry);
    candidate.wet = int(storedWet);
    candidate.mask = in[5];
    if (!candidate.recordValid()) return false;
    *this = candidate;
    return true;
  }

 private:
  static bool inRange(int raw) { return raw >= 0 && raw <= kMaxRaw; }

  static int64_t distance(int a, int b) {
    const int64_t delta = int64_t(a) - b;
    return delta >= 0 ? delta : -delta;
  }

  bool recordValid() const {
    return !(mask & uint8_t(~3U)) && inRange(dry) && inRange(wet) &&
           (mask != 3 || distance(dry, wet) >= kMinSpan);
  }

  static uint32_t load32(const uint8_t* bytes) {
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
           (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
  }

  static void store32(uint8_t* bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = uint8_t(value >> (8 * i));
  }

  static uint32_t crc32(const uint8_t* bytes, size_t count) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < count; ++i) {
      crc ^= bytes[i];
      for (unsigned bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return crc ^ 0xFFFFFFFFU;
  }
};

#endif
