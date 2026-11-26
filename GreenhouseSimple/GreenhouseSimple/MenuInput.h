#ifndef GREENHOUSE_SIMPLE_MENU_INPUT_H
#define GREENHOUSE_SIMPLE_MENU_INPUT_H

#include <stdint.h>
#include "SimpleControl.h"

enum SimplePage {
  PAGE_WARM = 0,
  PAGE_UV = 1,
  PAGE_SOIL = 2,
  PAGE_FANS = 3,
  PAGE_COUNT = 4
};

struct MenuSelection {
  int selected = PAGE_WARM;

  void previous() { selected = (selected + PAGE_COUNT - 1) % PAGE_COUNT; }
  void next() { selected = (selected + 1) % PAGE_COUNT; }

  // A sensor page has no load to toggle. Page order differs from channel order.
  int channel() const {
    switch (selected) {
      case PAGE_WARM: return WARM;
      case PAGE_UV: return UV;
      case PAGE_FANS: return FANS;
      default: return -1;
    }
  }
};

// Four active-HIGH touch outputs occupy PCF8575 bits 8..11.
// Events: 0 previous, 1 next, 2 activate selection, 3 all off.
// poll() returns at most one event; a long press does not repeat.
class TouchButtons {
 public:
  static constexpr uint32_t kDebounceMs = 40;

  // Call on loss of the I2C link. A held input must be released after recovery
  // before it can generate an action, just as at initial startup.
  void reset() { initialized_ = false; }

  int poll(uint16_t rawPCF, uint32_t now) {
    const uint8_t inputs = uint8_t((rawPCF >> 8) & 0x0F);
    if (!initialized_) {
      for (unsigned key = 0; key < 4; ++key) {
        candidate_[key] = (inputs & (1U << key)) != 0;
        changedAt_[key] = now;
        armed_[key] = false;
      }
      initialized_ = true;
      return -1;
    }

    uint8_t pressed = 0;
    for (unsigned key = 0; key < 4; ++key) {
      const bool high = (inputs & (1U << key)) != 0;
      if (high != candidate_[key]) {
        candidate_[key] = high;
        changedAt_[key] = now;
      }
      if (uint32_t(now - changedAt_[key]) < kDebounceMs) continue;

      if (!high) {
        armed_[key] = true;
      } else if (armed_[key]) {
        armed_[key] = false;
        pressed |= uint8_t(1U << key);
      }
    }

    if (pressed & 0x08) return 3;
    // Do not queue activation behind ALL OFF, even when its HIGH edge has not
    // yet passed debounce. Other pressed events in that poll are consumed.
    if ((inputs & 0x08) || !armed_[3]) return -1;
    // Ambiguous navigation/activation chords must not change a load.
    if (!pressed || (pressed & uint8_t(pressed - 1))) return -1;
    for (int key = 0; key < 3; ++key) {
      if (pressed & (1U << key)) return key;
    }
    return -1;
  }

 private:
  uint32_t changedAt_[4] = {};
  bool candidate_[4] = {};
  bool armed_[4] = {};
  bool initialized_ = false;
};

#endif
