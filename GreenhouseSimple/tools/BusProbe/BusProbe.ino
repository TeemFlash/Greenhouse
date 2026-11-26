// NodeMCU ESP8266 / ESP8266 core 3.1.2. Serial: 115200 baud.
// Diagnostic only: sends a 7-bit write address followed by STOP, NO DATA bytes.
// It never writes PCF8575 output registers and does not force loads OFF.
// Scans once at boot; send 'r' to repeat. Only GPIO4 and GPIO5 are touched.
#include <Arduino.h>
#include <Wire.h>

static const uint32_t HALF_US = 50;
static const uint32_t STRETCH_US = 2500;

void logPins(const char *stage) {
  Serial.printf("%s GPO[5:4]=0x%02lX GPIO4=%d GPIO5=%d\n", stage,
                (unsigned long)(GPO & ((1UL << 4) | (1UL << 5))),
                digitalRead(4), digitalRead(5));
}

// Independent open-drain implementation. HIGH means INPUT_PULLUP, never a
// driven HIGH. Explicitly clear each output latch before enabling LOW output.
class SlowBus {
 public:
  enum { ACK = 0, NACK = 1, SCL_TIMEOUT = 2, SDA_STUCK_LOW = 3 };
  int sample5 = -1, sample40 = -1;

  SlowBus(uint8_t sda, uint8_t scl) : sda_(sda), scl_(scl) {}

  void releaseBus() {
    release(sda_);
    release(scl_);
    delayMicroseconds(HALF_US);
  }

  int probe(uint8_t address) {
    sample5 = sample40 = -1;
    release(sda_);
    if (!clockHigh()) return abortBus(SCL_TIMEOUT);
    delayMicroseconds(HALF_US);
    if (!digitalRead(sda_)) return abortBus(SDA_STUCK_LOW);
    low(sda_);  // START: SDA falls while SCL is HIGH.
    delayMicroseconds(HALF_US);
    low(scl_);

    const uint8_t addressByte = uint8_t(address << 1);  // R/W = write.
    for (uint8_t mask = 0x80; mask; mask >>= 1) {
      if (addressByte & mask) release(sda_);
      else low(sda_);
      delayMicroseconds(HALF_US);
      if (!clockHigh()) return abortBus(SCL_TIMEOUT);
      delayMicroseconds(HALF_US);
      low(scl_);
    }
    // Ninth clock belongs to the slave. Master leaves SDA released.
    release(sda_);
    delayMicroseconds(HALF_US);
    if (!clockHigh()) return abortBus(SCL_TIMEOUT);
    delayMicroseconds(5);
    sample5 = digitalRead(sda_);
    delayMicroseconds(35);
    sample40 = digitalRead(sda_);
    delayMicroseconds(10);
    low(scl_);

    // STOP, without transmitting any data/register/output byte.
    low(sda_);
    delayMicroseconds(HALF_US);
    if (!clockHigh()) return abortBus(SCL_TIMEOUT);
    delayMicroseconds(HALF_US);
    release(sda_);
    delayMicroseconds(HALF_US);
    return sample40 == LOW ? ACK : NACK;
  }

 private:
  uint8_t sda_, scl_;

  static void release(uint8_t pin) { pinMode(pin, INPUT_PULLUP); }
  static void low(uint8_t pin) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }
  bool clockHigh() {
    release(scl_);
    const uint32_t start = micros();
    while (!digitalRead(scl_)) {
      if (uint32_t(micros() - start) >= STRETCH_US) return false;
      delayMicroseconds(1);
    }
    return true;
  }
  int abortBus(int status) {
    // If SCL cannot rise or SDA is held LOW, a valid STOP is impossible.
    // Release both lines and report the fault instead of driving HIGH.
    releaseBus();
    return status;
  }
};

void scanWire(uint8_t sda, uint8_t scl, const char *label) {
  Serial.printf("\nWIRE %s SDA=GPIO%u SCL=GPIO%u clock=100000\n", label, sda, scl);
  logPins("before Wire.begin");
  Wire.begin(sda, scl);
  Wire.setClock(100000);
  Wire.setClockStretchLimit(STRETCH_US);
  logPins("after Wire.begin");
  unsigned ack = 0, codes[6] = {};
  for (uint8_t address = 0x08; address <= 0x77; ++address) {
    Wire.beginTransmission(address);
    // Intentionally no Wire.write(): address phase only.
    const uint8_t status = Wire.endTransmission(true);
    codes[status < 5 ? status : 5]++;
    if (status == 0) {
      ++ack;
      Serial.printf("WIRE ACK 0x%02X\n", address);
    }
    if (address == 0x20 || address == 0x40 || address == 0x48)
      Serial.printf("WIRE target 0x%02X status=%u\n", address, status);
    yield();
  }
  Serial.printf("WIRE SUMMARY ACK=%u rc0=%u rc1=%u rc2=%u rc3=%u rc4=%u rc5plus=%u\n",
                ack, codes[0], codes[1], codes[2], codes[3], codes[4], codes[5]);
  logPins("after Wire scan");
}

void scanSlow(uint8_t sda, uint8_t scl) {
  Serial.printf("\nBITBANG SDA=GPIO%u SCL=GPIO%u half_period_us=%lu\n",
                sda, scl, (unsigned long)HALF_US);
  SlowBus bus(sda, scl);
  bus.releaseBus();
  logPins("before bitbang");
  unsigned codes[4] = {};
  for (uint8_t address = 0x08; address <= 0x77; ++address) {
    const int status = bus.probe(address);
    codes[status]++;
    if (status == SlowBus::ACK) Serial.printf("BITBANG ACK 0x%02X\n", address);
    if (address == 0x20 || address == 0x40 || address == 0x48)
      Serial.printf("BITBANG target 0x%02X status=%d ACK_SDA_5us=%d ACK_SDA_40us=%d\n",
                    address, status, bus.sample5, bus.sample40);
    yield();
  }
  bus.releaseBus();
  Serial.printf("BITBANG SUMMARY ACK=%u NACK=%u SCL_TIMEOUT=%u SDA_STUCK_LOW=%u\n",
                codes[0], codes[1], codes[2], codes[3]);
  logPins("after bitbang");
}

void runProbes() {
  logPins("RUN INITIAL (before configuring GPIO)");
  scanWire(4, 5, "initial standard pins");
  scanSlow(4, 5);
  // Bitbang LOW transitions explicitly cleared GPIO output latches.
  scanWire(4, 5, "standard pins after bitbang");
  scanWire(5, 4, "swapped pins");
  scanSlow(5, 4);
  scanWire(5, 4, "swapped pins after bitbang");
  pinMode(4, INPUT_PULLUP);
  pinMode(5, INPUT_PULLUP);
  Serial.println("\nBUSPROBE COMPLETE. Lines released. Send r to repeat.");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nBOOT BUSPROBE_V1 ADDRESS_ONLY_NO_OUTPUT_WRITES");
  Serial.printf("CPU_MHz=%u reset=%s\n", ESP.getCpuFreqMHz(),
                ESP.getResetReason().c_str());
  runProbes();
}

void loop() {
  if (Serial.available()) {
    const char command = Serial.read();
    if (command == 'r' || command == 'R') runProbes();
  }
  delay(10);
}
