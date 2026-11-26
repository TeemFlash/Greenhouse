#ifndef GREENHOUSE_TEST_WIRE_H
#define GREENHOUSE_TEST_WIRE_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <initializer_list>
#include <vector>

// Every transaction is checked against the expected bus dialogue. This fake
// cannot silently accept an extra poll or a write to an actuator's address.
class FakeTwoWire {
 public:
  void expectWrite(std::initializer_list<uint8_t> bytes, bool stop = true,
                   uint8_t result = 0) {
    Expected e;
    e.kind = Write;
    e.bytes.assign(bytes);
    e.stop = stop;
    e.result = result;
    expected_.push_back(e);
  }

  void expectRead(std::initializer_list<int> bytes, uint8_t count = 2) {
    Expected e;
    e.kind = Read;
    e.readBytes.assign(bytes);
    e.result = count;
    expected_.push_back(e);
  }

  void beginTransmission(uint8_t address) {
    assert(address == 0x48);
    outgoing_.clear();
  }

  size_t write(uint8_t value) {
    outgoing_.push_back(value);
    return 1;
  }

  uint8_t endTransmission(bool stop = true) {
    assert(!expected_.empty());
    const Expected e = expected_.front();
    expected_.pop_front();
    assert(e.kind == Write);
    assert(e.stop == stop);
    assert(e.bytes == outgoing_);
    ++transmissions;
    return e.result;
  }

  uint8_t requestFrom(uint8_t address, uint8_t count) {
    assert(address == 0x48 && count == 2);
    assert(!expected_.empty());
    const Expected e = expected_.front();
    expected_.pop_front();
    assert(e.kind == Read);
    incoming_ = e.readBytes;
    readAt_ = 0;
    ++requests;
    return e.result;
  }

  int available() const { return int(incoming_.size() - readAt_); }

  int read() {
    return readAt_ < incoming_.size() ? incoming_[readAt_++] : -1;
  }

  void done() const { assert(expected_.empty()); }
  unsigned transmissions = 0;
  unsigned requests = 0;

 private:
  enum Kind { Write, Read };
  struct Expected {
    Kind kind = Write;
    std::vector<uint8_t> bytes;
    std::vector<int> readBytes;
    bool stop = true;
    uint8_t result = 0;
  };
  std::deque<Expected> expected_;
  std::vector<uint8_t> outgoing_;
  std::vector<int> incoming_;
  size_t readAt_ = 0;
};

extern FakeTwoWire Wire;

#endif
