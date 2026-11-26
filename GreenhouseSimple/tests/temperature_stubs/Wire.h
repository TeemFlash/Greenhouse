#ifndef GREENHOUSE_TEST_TEMPERATURE_WIRE_H
#define GREENHOUSE_TEST_TEMPERATURE_WIRE_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <initializer_list>
#include <vector>

// A separate fake avoids changing the ADS1115 tests or their Wire contract.
// Unexpected transactions, wrong commands and writes to other addresses fail.
class TemperatureFakeWire {
 public:
  void expectStart(uint8_t result = 0) {
    Expected e;
    e.kind = Write;
    e.result = result;
    expected_.push_back(e);
  }

  void expectRead(std::initializer_list<int> bytes, uint8_t count = 3) {
    Expected e;
    e.kind = Read;
    e.bytes.assign(bytes);
    e.result = count;
    expected_.push_back(e);
  }

  void beginTransmission(uint8_t address) {
    assert(address == 0x40);
    outgoing_.clear();
  }

  size_t write(uint8_t value) {
    outgoing_.push_back(value);
    const size_t result = nextWriteResult;
    nextWriteResult = 1;
    return result;
  }

  uint8_t endTransmission(bool stop = true) {
    assert(stop);
    assert(outgoing_.size() == 1 && outgoing_[0] == 0xF3);
    assert(!expected_.empty());
    const Expected e = expected_.front();
    expected_.pop_front();
    assert(e.kind == Write);
    ++transmissions;
    return e.result;
  }

  uint8_t requestFrom(uint8_t address, uint8_t count) {
    assert(address == 0x40 && count == 3);
    assert(!expected_.empty());
    const Expected e = expected_.front();
    expected_.pop_front();
    assert(e.kind == Read);
    incoming_ = e.bytes;
    readAt_ = 0;
    ++requests;
    return e.result;
  }

  int available() const { return int(incoming_.size() - readAt_); }
  int read() { return readAt_ < incoming_.size() ? incoming_[readAt_++] : -1; }
  void done() const { assert(expected_.empty()); }
  unsigned transmissions = 0;
  unsigned requests = 0;
  size_t nextWriteResult = 1;

 private:
  enum Kind { Write, Read };
  struct Expected {
    Kind kind = Write;
    std::vector<int> bytes;
    uint8_t result = 0;
  };
  std::deque<Expected> expected_;
  std::vector<uint8_t> outgoing_;
  std::vector<int> incoming_;
  size_t readAt_ = 0;
};

extern TemperatureFakeWire Wire;

#endif
