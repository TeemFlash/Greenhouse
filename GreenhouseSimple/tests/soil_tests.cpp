#include "../GreenhouseSimple/SoilAdc.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

FakeTwoWire Wire;

static void resetWire() {
  Wire.done();
  Wire = FakeTwoWire();
}

static void expectStart(uint8_t error = 0) {
  Wire.expectWrite({1, 0xC3, 0x83}, true, error);
}

static void expectRegister(uint8_t reg, uint16_t value) {
  Wire.expectWrite({reg}, false);
  Wire.expectRead({int(value >> 8), int(value & 255)});
}

static void expectSample(uint16_t raw) {
  expectRegister(1, 0xC383);
  expectRegister(0, raw);
}

static void start(SoilAdc& adc, SoilReading& reading, uint32_t now) {
  expectStart();
  assert(!adc.tick(now, reading));
  Wire.done();
}

static void testModelFreshness() {
  SoilReading reading;
  assert(!reading.valid && reading.raw == 0 && reading.millivolts == 0);
  assert(!reading.fresh(0));
  reading.valid = true;
  reading.sampledAt = 300;
  assert(reading.fresh(2300));
  assert(!reading.fresh(2301));
  reading.sampledAt = UINT32_MAX - 20U;
  assert(reading.fresh(5));
  assert(!reading.fresh(1980));  // 2001 ms across wraparound.
}

static void testRealSamplesAndCadence() {
  resetWire();
  SoilAdc adc;
  SoilReading reading;
  start(adc, reading, 0);
  assert(!adc.tick(9, reading));
  expectSample(16004);
  assert(adc.tick(10, reading));
  assert(reading.valid && reading.raw == 16004);
  assert(reading.millivolts == 2001 && reading.sampledAt == 10);
  assert(!adc.tick(499, reading));
  start(adc, reading, 500);
  expectSample(26400);
  assert(adc.tick(510, reading));
  assert(reading.millivolts == 3300);
  // A new sample must refresh the UI/timestamp even when its value is equal.
  start(adc, reading, 1000);
  expectSample(26400);
  assert(adc.tick(1010, reading) && reading.sampledAt == 1010);
  Wire.done();
}

static void testExplicitRequestAndActiveConversion() {
  resetWire();
  SoilAdc adc;
  SoilReading reading;
  start(adc, reading, 100);
  adc.request();
  assert(!adc.tick(109, reading));  // Do not restart a pending conversion.
  expectSample(0);
  assert(adc.tick(110, reading) && reading.valid && reading.millivolts == 0);
  assert(!adc.tick(111, reading));  // The pending conversion satisfied request.
  adc.request();
  start(adc, reading, 112);
  expectSample(8);
  assert(adc.tick(122, reading) && reading.millivolts == 1);
  Wire.done();
}

static void testNackBackoffAndRecovery() {
  resetWire();
  SoilAdc adc;
  SoilReading reading;
  start(adc, reading, 0);
  expectSample(8000);
  assert(adc.tick(10, reading));
  adc.request();
  expectStart(2);
  assert(adc.tick(20, reading));  // Valid -> invalid is a visible change.
  assert(!reading.valid && reading.sampledAt == 10);
  for (uint32_t now = 21; now < 520; ++now) {
    adc.request();
    assert(!adc.tick(now, reading));  // Requests cannot hammer an absent ADC.
  }
  start(adc, reading, 520);
  expectSample(12000);
  assert(adc.tick(530, reading) && reading.valid && reading.raw == 12000);
  Wire.done();
}

static void testBusyPollingAndTimeout() {
  resetWire();
  SoilAdc adc;
  SoilReading reading;
  start(adc, reading, 0);
  for (uint32_t now = 10; now <= 100; now += 10) {
    expectRegister(1, 0x4383);  // Correct configuration, OS=0.
    assert(!adc.tick(now, reading));
    assert(!adc.tick(now + 1, reading));
  }
  assert(!reading.valid);
  // Timeout was at 100, therefore no new attempt until 600.
  adc.request();
  assert(!adc.tick(599, reading));
  start(adc, reading, 600);
  expectSample(1000);
  assert(adc.tick(610, reading));
  Wire.done();
}

static void testReadyAfterSlowLoop() {
  resetWire();
  SoilAdc adc;
  SoilReading reading;
  start(adc, reading, 0);
  // Rendering may postpone polling. A completed conversion is still valid.
  expectSample(800);
  assert(adc.tick(130, reading) && reading.millivolts == 100);
  Wire.done();
}

static void testWrongConfiguration() {
  const uint16_t wrong[] = {
      0xD383,  // AIN1 instead of AIN0.
      0xC583,  // Incorrect gain.
      0xC283,  // Continuous mode.
      0xC3A3,  // Incorrect rate.
      0x4380,  // Busy, but comparator configuration does not match.
  };
  for (uint16_t status : wrong) {
    resetWire();
    SoilAdc adc;
    SoilReading reading;
    start(adc, reading, 0);
    expectRegister(1, status);
    assert(!adc.tick(10, reading) && !reading.valid);
    assert(!adc.tick(509, reading));  // Failure must enter retry backoff.
    start(adc, reading, 510);
    Wire.done();
  }
}

static void testRegisterNacksAndShortReads() {
  for (unsigned failAt = 0; failAt < 8; ++failAt) {
    resetWire();
    SoilAdc adc;
    SoilReading reading;
    start(adc, reading, 0);
    expectSample(8000);
    assert(adc.tick(10, reading));
    adc.request();
    start(adc, reading, 20);
    const uint8_t reg = failAt >= 4 ? 0 : 1;
    if (reg == 0) expectRegister(1, 0xC383);
    if (failAt % 4 == 0) {
      Wire.expectWrite({reg}, false, 2);  // Register-pointer address NACK.
    } else {
      Wire.expectWrite({reg}, false);
      if (failAt % 4 == 1) Wire.expectRead({0xC3}, 1);
      if (failAt % 4 == 2) Wire.expectRead({0xC3}, 2);  // Count/buffer disagree.
      if (failAt % 4 == 3) Wire.expectRead({0xC3, -1}, 2);
    }
    assert(adc.tick(30, reading) && !reading.valid);
    assert(reading.sampledAt == 10);  // Never publish a partial conversion.
    assert(!adc.tick(529, reading));
    start(adc, reading, 530);
    Wire.done();
  }
}

static void testInvalidSignedAndOverRangeData() {
  const uint16_t invalid[] = {0xFFFF, 0x8000, 26401, 32767};
  for (uint16_t raw : invalid) {
    resetWire();
    SoilAdc adc;
    SoilReading reading;
    start(adc, reading, 0);
    expectSample(raw);
    assert(!adc.tick(10, reading));
    assert(!reading.valid && reading.raw == 0 && reading.sampledAt == 0);
    Wire.done();
  }
}

static void testStaleInvalidation() {
  resetWire();
  SoilAdc adc;
  SoilReading reading;
  start(adc, reading, 0);
  expectSample(4000);
  assert(adc.tick(10, reading));
  // A stalled caller must not retain valid=true on its first resumed tick.
  expectStart();
  assert(adc.tick(2011, reading) && !reading.valid);
  expectSample(4008);
  assert(adc.tick(2021, reading) && reading.fresh(2021));
  Wire.done();
}

static void testClockWraparound() {
  resetWire();
  SoilAdc adc;
  SoilReading reading;
  const uint32_t begin = UINT32_MAX - 5U;
  start(adc, reading, begin);
  assert(!adc.tick(3, reading));  // 9 ms elapsed.
  expectSample(16000);
  assert(adc.tick(4, reading));  // 10 ms elapsed.
  assert(reading.raw == 16000 && reading.sampledAt == 4);
  assert(!adc.tick(493, reading));
  start(adc, reading, 494);
  Wire.done();

  resetWire();
  SoilAdc failed;
  SoilReading absent;
  expectStart(2);
  assert(!failed.tick(begin, absent));
  failed.request();
  assert(!failed.tick(493, absent));
  start(failed, absent, 494);
  Wire.done();
}

int main() {
  testModelFreshness();
  testRealSamplesAndCadence();
  testExplicitRequestAndActiveConversion();
  testNackBackoffAndRecovery();
  testBusyPollingAndTimeout();
  testReadyAfterSlowLoop();
  testWrongConfiguration();
  testRegisterNacksAndShortReads();
  testInvalidSignedAndOverRangeData();
  testStaleInvalidation();
  testClockWraparound();
  puts("All soil ADC state-machine and freshness tests passed.");
}
