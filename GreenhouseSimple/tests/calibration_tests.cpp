#include "../GreenhouseSimple/SoilCalibration.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static SoilReading reading(int raw, uint32_t sampledAt = 100) {
  SoilReading result;
  result.valid = true;
  result.raw = raw;
  result.sampledAt = sampledAt;
  return result;
}

static void same(const SoilCalibration& a, const SoilCalibration& b) {
  assert(a.dry == b.dry && a.wet == b.wet && a.mask == b.mask);
}

static SoilCalibration calibrated(int dry, int wet) {
  SoilCalibration result;
  assert(result.capture(false, dry));
  assert(result.capture(true, wet));
  assert(result.valid());
  return result;
}

static void testUncalibratedAndFreshness() {
  SoilCalibration calibration;
  assert(!calibration.valid());
  assert(calibration.percent(reading(1000), 100) == -1);
  assert(calibration.capture(false, 2000));
  assert(calibration.mask == 1 && !calibration.valid());
  assert(calibration.percent(reading(1000), 100) == -1);
  assert(calibration.capture(true, 1000));
  assert(calibration.percent(reading(1500), 2100) == 50);
  assert(calibration.percent(reading(1500), 2101) == -1);
  SoilReading invalid = reading(1500);
  invalid.valid = false;
  assert(calibration.percent(invalid, 100) == -1);
  const SoilReading wrapping = reading(1500, UINT32_MAX - 1000U);
  assert(calibration.percent(wrapping, 999) == 50);   // 2000 ms old.
  assert(calibration.percent(wrapping, 1000) == -1);  // 2001 ms old.
  calibration.clear();
  assert(calibration.mask == 0 && calibration.dry == 0 && calibration.wet == 0);
  assert(calibration.percent(reading(1500), 100) == -1);
}

static void testBothSensorDirectionsAndRounding() {
  for (unsigned reversed = 0; reversed < 2; ++reversed) {
    const int dry = reversed ? 2000 : 1000;
    const int wet = reversed ? 1000 : 2000;
    const int direction = reversed ? -1 : 1;
    SoilCalibration calibration = calibrated(dry, wet);
    assert(calibration.percent(reading(dry), 100) == 0);
    assert(calibration.percent(reading(wet), 100) == 100);
    assert(calibration.percent(reading(1500), 100) == 50);
    assert(calibration.percent(reading(dry + direction * 4), 100) == 0);
    assert(calibration.percent(reading(dry + direction * 5), 100) == 1);
    assert(calibration.percent(reading(dry + direction * 994), 100) == 99);
    assert(calibration.percent(reading(dry + direction * 995), 100) == 100);
    assert(calibration.percent(reading(dry - direction * 100), 100) == 0);
    assert(calibration.percent(reading(wet + direction * 100), 100) == 100);
    // Public model must clamp even an extreme caller value without overflow.
    assert(calibration.percent(reading(INT_MIN), 100) == (reversed ? 100 : 0));
    assert(calibration.percent(reading(INT_MAX), 100) == (reversed ? 0 : 100));
  }
  SoilCalibration fullRange = calibrated(0, 26400);
  assert(fullRange.percent(reading(13200), 100) == 50);
}

static void testCapturesPreserveStateAndMinimumSpan() {
  SoilCalibration calibration;
  assert(calibration.capture(true, 1000));  // Wet may be captured first.
  assert(calibration.mask == 2 && !calibration.valid());
  const SoilCalibration partial = calibration;
  assert(!calibration.capture(false, 1499));
  same(calibration, partial);
  assert(!calibration.capture(false, -1));
  same(calibration, partial);
  assert(!calibration.capture(false, 26401));
  same(calibration, partial);
  assert(calibration.capture(false, 1500));  // Exactly 500 is valid.
  assert(calibration.valid());
  const SoilCalibration original = calibration;
  assert(!calibration.capture(true, 1001));  // Would shrink the span to 499.
  same(calibration, original);
  assert(!calibration.capture(false, 1499));
  same(calibration, original);
  assert(!calibration.capture(true, INT_MAX));
  same(calibration, original);
  assert(calibration.capture(true, 2000));  // May reverse the response.
  assert(calibration.valid() && calibration.percent(reading(1750), 100) == 50);
  calibration.mask = 7;
  const SoilCalibration badFlags = calibration;
  assert(!calibration.capture(false, 0));
  same(calibration, badFlags);
  assert(!calibration.valid());
}

static void testGoldenRecordAndPartialRoundTrips() {
  const uint8_t expected[20] = {
      0x53, 0x43, 0x41, 0x4C, 0x01, 0x03, 0x00, 0x00,
      0xF0, 0x55, 0x00, 0x00, 0x70, 0x17, 0x00, 0x00,
      0x4D, 0xF5, 0xBA, 0xD3
  };  // Independently generated using Python struct.pack and zlib.crc32.
  static_assert(SoilCalibration::kStorageSize == sizeof(expected), "record size");
  uint8_t record[SoilCalibration::kStorageSize];
  const SoilCalibration original = calibrated(22000, 6000);
  original.encode(record);
  assert(memcmp(record, expected, sizeof(record)) == 0);
  SoilCalibration loaded;
  assert(loaded.decode(expected));
  same(loaded, original);

  for (unsigned mask = 0; mask <= 3; ++mask) {
    SoilCalibration source;
    if (mask & 1) assert(source.capture(false, 26400));
    if (mask & 2) assert(source.capture(true, 0));
    source.encode(record);
    loaded = original;
    assert(loaded.decode(record));
    same(source, loaded);
    assert(loaded.valid() == (mask == 3));
  }
  // Explicit byte access also permits unaligned storage.
  uint8_t unaligned[SoilCalibration::kStorageSize + 1] = {};
  original.encode(unaligned + 1);
  loaded.clear();
  assert(loaded.decode(unaligned + 1));
  same(loaded, original);
}

// Rewrite CRC after intentional format mutations, so the decoder's format and
// numeric checks are exercised independently of its corruption detection.
static void repairCrc(uint8_t* record) {
  uint32_t crc = UINT32_MAX;
  for (unsigned i = 0; i < 16; ++i) {
    crc ^= record[i];
    for (unsigned bit = 0; bit < 8; ++bit) {
      const bool carry = (crc & 1) != 0;
      crc >>= 1;
      if (carry) crc ^= 0xEDB88320U;
    }
  }
  crc ^= UINT32_MAX;
  for (unsigned i = 0; i < 4; ++i) record[16 + i] = uint8_t(crc >> (8 * i));
}

static void rejectPreserving(const uint8_t* record) {
  SoilCalibration destination = calibrated(4000, 8000);
  const SoilCalibration before = destination;
  assert(!destination.decode(record));
  same(destination, before);
}

static void testCorruptionVersionFlagsAndBounds() {
  const SoilCalibration original = calibrated(22000, 6000);
  uint8_t record[SoilCalibration::kStorageSize];
  for (unsigned byte = 0; byte < sizeof(record); ++byte) {
    for (unsigned bit = 0; bit < 8; ++bit) {
      original.encode(record);
      record[byte] ^= uint8_t(1U << bit);
      rejectPreserving(record);
    }
  }
  memset(record, 0xFF, sizeof(record));  // Erased EEPROM.
  rejectPreserving(record);
  memset(record, 0, sizeof(record));
  rejectPreserving(record);

  const unsigned formatOffsets[] = {0, 1, 2, 3, 4, 5, 6, 7};
  for (unsigned offset : formatOffsets) {
    original.encode(record);
    record[offset] = offset == 5 ? 0x83 : uint8_t(record[offset] + 1);
    repairCrc(record);
    rejectPreserving(record);
  }
  const int invalidValues[] = {-1, 26401, INT_MAX};
  for (int value : invalidValues) {
    SoilCalibration source = original;
    source.dry = value;
    source.encode(record);  // Correct CRC cannot legitimize out-of-range points.
    rejectPreserving(record);
    source = original;
    source.wet = value;
    source.encode(record);
    rejectPreserving(record);
  }
  for (int span = -499; span <= 499; ++span) {
    SoilCalibration source = original;
    source.wet = source.dry + span;
    source.encode(record);
    rejectPreserving(record);
  }
  // Bounds and flags are checked for partial records too.
  SoilCalibration partial;
  partial.mask = 1;
  partial.dry = 26401;
  partial.encode(record);
  rejectPreserving(record);
  partial.clear();
  partial.mask = 2;
  partial.wet = -1;
  partial.encode(record);
  rejectPreserving(record);
}

int main() {
  testUncalibratedAndFreshness();
  testBothSensorDirectionsAndRounding();
  testCapturesPreserveStateAndMinimumSpan();
  testGoldenRecordAndPartialRoundTrips();
  testCorruptionVersionFlagsAndBounds();
  puts("All soil-calibration and persistent-record tests passed.");
}
