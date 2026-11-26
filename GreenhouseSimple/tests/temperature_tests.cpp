#include "../GreenhouseSimple/TemperatureHtu.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

TemperatureFakeWire Wire;

static void resetWire() {
  Wire.done();
  Wire = TemperatureFakeWire();
}

static void start(TemperatureHtu& sensor, TemperatureReading& reading, uint32_t now) {
  Wire.expectStart();
  assert(!sensor.tick(now, reading));
  Wire.done();
}

// Independent fixed wire vectors. 0x683A/0x7C is the manufacturer's CRC
// example; 0x6838/0x1E is the same numerical sample with temperature status 00.
static void healthy() { Wire.expectRead({0x68, 0x38, 0x1E}); }

static void testModelFreshness() {
  TemperatureReading reading;
  assert(!reading.valid && reading.celsius == 0.0f && reading.sampledAt == 0);
  assert(!reading.fresh(0));
  reading.valid = true;
  reading.sampledAt = 100;
  assert(reading.fresh(5100));
  assert(!reading.fresh(5101));
  reading.sampledAt = UINT32_MAX - 20U;
  assert(reading.fresh(4979));
  assert(!reading.fresh(4980));
}

static void testHealthyAndPeriod() {
  resetWire();
  TemperatureHtu sensor;
  TemperatureReading reading;
  start(sensor, reading, 0);
  for (uint32_t now = 1; now < 60; ++now) assert(!sensor.tick(now, reading));
  healthy();
  assert(sensor.tick(60, reading));
  assert(reading.valid && reading.sampledAt == 60);
  assert(fabsf(reading.celsius - 24.686401f) < 0.0001f);
  assert(!sensor.tick(1499, reading));
  start(sensor, reading, 1500);
  healthy();
  assert(sensor.tick(1560, reading));  // Same value still refreshes timestamp.
  assert(reading.sampledAt == 1560);
  Wire.done();
}

static void testRequestAndSlowLoop() {
  resetWire();
  TemperatureHtu sensor;
  TemperatureReading reading;
  start(sensor, reading, 100);
  sensor.request();
  assert(!sensor.tick(159, reading));
  // A long display frame cannot turn an already-completed conversion invalid.
  healthy();
  assert(sensor.tick(310, reading));
  assert(!sensor.tick(311, reading));  // In-flight request was satisfied.
  sensor.request();
  start(sensor, reading, 312);
  Wire.expectRead({0x80, 0x00, 0x23});
  assert(sensor.tick(372, reading));
  assert(fabsf(reading.celsius - 41.01f) < 0.0001f);
  Wire.done();
}

static void testCommandNackBackoffRecovery() {
  resetWire();
  TemperatureHtu sensor;
  TemperatureReading reading;
  start(sensor, reading, 0);
  healthy();
  assert(sensor.tick(60, reading));
  sensor.request();
  Wire.expectStart(2);
  assert(!sensor.tick(61, reading));
  assert(reading.valid && reading.sampledAt == 60);
  for (uint32_t now = 62; now < 1561; ++now) {
    sensor.request();
    assert(!sensor.tick(now, reading));
  }
  start(sensor, reading, 1561);
  healthy();
  assert(sensor.tick(1621, reading) && reading.valid);
  Wire.done();
}

static void testCommandQueueAndDataNack() {
  for (unsigned failure = 0; failure < 2; ++failure) {
    resetWire();
    TemperatureHtu sensor;
    TemperatureReading reading;
    if (failure == 0) Wire.nextWriteResult = 0;
    Wire.expectStart(failure == 1 ? 3 : 0);
    assert(!sensor.tick(0, reading));
    sensor.request();
    assert(!sensor.tick(1499, reading));
    start(sensor, reading, 1500);
    Wire.done();
  }
}

static void testReadFailures() {
  for (unsigned failure = 0; failure < 5; ++failure) {
    resetWire();
    TemperatureHtu sensor;
    TemperatureReading reading;
    start(sensor, reading, 0);
    healthy();
    assert(sensor.tick(60, reading));
    sensor.request();
    start(sensor, reading, 100);
    switch (failure) {
      case 0: Wire.expectRead({}, 0); break;  // Read address NACK, after 60 ms.
      case 1: Wire.expectRead({0x68, 0x38}, 2); break;
      case 2: Wire.expectRead({0x68, 0x38}, 3); break;  // Buffer/count mismatch.
      case 3: Wire.expectRead({0x68, -1, 0x1E}); break;
      case 4: Wire.expectRead({0x80, 0x00, 0x22}); break;  // Different value, bad CRC.
    }
    assert(!sensor.tick(160, reading) && reading.valid);
    assert(reading.sampledAt == 60);
    assert(fabsf(reading.celsius - 24.686401f) < 0.0001f);
    sensor.request();
    assert(!sensor.tick(1659, reading));
    start(sensor, reading, 1660);
    healthy();
    assert(sensor.tick(1720, reading) && reading.valid);
    Wire.done();
  }
}

static void testFailuresCannotExtendFreshness() {
  resetWire();
  TemperatureHtu sensor;
  TemperatureReading reading;
  start(sensor, reading, 0);
  healthy();
  assert(sensor.tick(60, reading));
  sensor.request();
  start(sensor, reading, 100);
  Wire.expectRead({0x80, 0x00, 0x22});  // 41.01 C payload with invalid CRC.
  assert(!sensor.tick(160, reading));
  assert(reading.fresh(160) && reading.sampledAt == 60);
  assert(fabsf(reading.celsius - 24.686401f) < 0.0001f);

  // Continued command failures must preserve neither a new value nor a new
  // timestamp. Retrying remains limited to 1500 ms after the last fault.
  assert(!sensor.tick(1659, reading));
  for (uint32_t now : {1660U, 3160U, 4660U}) {
    Wire.expectStart(2);
    assert(!sensor.tick(now, reading));
    assert(reading.valid && reading.sampledAt == 60);
    sensor.request();
    assert(!sensor.tick(now + 1, reading));
  }
  assert(!sensor.tick(5060, reading) && reading.fresh(5060));
  assert(sensor.tick(5061, reading) && !reading.valid);
  assert(!sensor.tick(5062, reading));  // Expiration is reported only once.
  assert(reading.sampledAt == 60);
  start(sensor, reading, 6160);
  Wire.expectRead({0x80, 0x00, 0x23});
  assert(sensor.tick(6220, reading) && reading.fresh(6220));
  assert(reading.sampledAt == 6220);
  assert(fabsf(reading.celsius - 41.01f) < 0.0001f);
  Wire.done();
}

static void testStatusBeforeMasking() {
  const int frames[][3] = {
      {0x68, 0x39, 0x2F},  // Unassigned bit 0.
      {0x68, 0x3A, 0x7C},  // Correct published CRC, but humidity status.
      {0x68, 0x3B, 0x4D},  // Diagnostic status.
  };
  for (const auto& frame : frames) {
    resetWire();
    TemperatureHtu sensor;
    TemperatureReading reading;
    start(sensor, reading, 0);
    Wire.expectRead({frame[0], frame[1], frame[2]});
    assert(!sensor.tick(60, reading) && !reading.valid);
    Wire.done();
  }
}

static void testValidRangeEdges() {
  const int frames[][3] = {
      {0x09, 0xFC, 0x3C},  // -39.99666 C.
      {0xFA, 0x5C, 0xF5},  // 124.99824 C.
  };
  for (const auto& frame : frames) {
    resetWire();
    TemperatureHtu sensor;
    TemperatureReading reading;
    start(sensor, reading, 0);
    Wire.expectRead({frame[0], frame[1], frame[2]});
    assert(sensor.tick(60, reading) && reading.valid);
    assert(isfinite(reading.celsius));
    assert(reading.celsius >= -40.0f && reading.celsius <= 125.0f);
    Wire.done();
  }
}

static void testOutOfRangeAndDiagnostics() {
  const int frames[][3] = {
      {0x09, 0xF8, 0xF8},  // Just below -40 C.
      {0xFA, 0x60, 0x4D},  // Just above 125 C.
      {0x00, 0x00, 0x00},  // Open-circuit diagnostic / -46.85 C.
      {0xFF, 0xFF, 0x2D},  // Short-circuit diagnostic.
      {0xFF, 0xFC, 0x7E},  // Even with temp status, value exceeds sensor range.
  };
  for (const auto& frame : frames) {
    resetWire();
    TemperatureHtu sensor;
    TemperatureReading reading;
    start(sensor, reading, 0);
    Wire.expectRead({frame[0], frame[1], frame[2]});
    assert(!sensor.tick(60, reading) && !reading.valid);
    assert(reading.celsius == 0.0f && reading.sampledAt == 0);
    Wire.done();
  }
}

static void testStaleInvalidation() {
  resetWire();
  TemperatureHtu sensor;
  TemperatureReading reading;
  start(sensor, reading, 0);
  healthy();
  assert(sensor.tick(60, reading));
  Wire.expectStart();
  assert(sensor.tick(5061, reading) && !reading.valid);
  healthy();
  assert(sensor.tick(5121, reading) && reading.fresh(5121));
  Wire.done();
}

static void testClockWraparound() {
  resetWire();
  TemperatureHtu sensor;
  TemperatureReading reading;
  const uint32_t begin = UINT32_MAX - 20U;
  start(sensor, reading, begin);
  assert(!sensor.tick(38, reading));
  healthy();
  assert(sensor.tick(39, reading) && reading.sampledAt == 39);
  assert(!sensor.tick(1478, reading));
  start(sensor, reading, 1479);
  Wire.done();

  resetWire();
  TemperatureHtu absent;
  TemperatureReading missing;
  Wire.expectStart(2);
  assert(!absent.tick(begin, missing));
  absent.request();
  assert(!absent.tick(1478, missing));
  start(absent, missing, 1479);
  Wire.done();
}

int main() {
  testModelFreshness();
  testHealthyAndPeriod();
  testRequestAndSlowLoop();
  testCommandNackBackoffRecovery();
  testCommandQueueAndDataNack();
  testReadFailures();
  testFailuresCannotExtendFreshness();
  testStatusBeforeMasking();
  testValidRangeEdges();
  testOutOfRangeAndDiagnostics();
  testStaleInvalidation();
  testClockWraparound();
  puts("All HTU21 air-temperature state-machine and freshness tests passed.");
}
