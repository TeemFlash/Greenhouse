#ifndef GREENHOUSE_SIMPLE_CONTROL_H
#define GREENHOUSE_SIMPLE_CONTROL_H

#include <stdint.h>

enum SimpleChannel { UV = 0, WARM = 1, FANS = 2 };

// PCF8575 pin P15 is bit 13 (P10..P17 are bits 8..15).
constexpr uint8_t kFanRelayBit = 13;
constexpr bool kFanRelayActiveLow = false;
constexpr uint16_t kFanRelayMask = uint16_t(1U << kFanRelayBit);
constexpr uint16_t kAllOffMask = uint16_t(
    (0xFFC3U & ~kFanRelayMask) |
    (kFanRelayActiveLow ? kFanRelayMask : 0U));

// Requested states only: a successful PCF8575 write does not prove a load works.
// Caller performs the physical all-lights-off interval before changing lamps.
struct SimpleControl {
  bool on[3] = {false, false, false};
  bool ioReady = false;

  void allOff() {
    on[UV] = false;
    on[WARM] = false;
    on[FANS] = false;
  }

  void link(bool ok) {
    ioReady = ok;
    if (!ok) allOff();
  }

  bool toggle(int channel) {
    if (!ioReady || channel < UV || channel > FANS) return false;
    const bool next = !on[channel];
    if (next && channel != FANS) {
      on[UV] = false;
      on[WARM] = false;
    }
    on[channel] = next;
    return true;
  }

  uint16_t mask() const {
    // P0/P1: active-low light relays; P15: separate fan relay.
    // Old motor-driver pins P2..P5 stay low, including both pump pins.
    // Unused pins and the input pins P10..P13 (bits 8..11) stay high.
    uint16_t result = kAllOffMask;
    if (!ioReady) return result;

    // Fail closed for a contradictory state, even if a caller changes on[].
    if (on[UV] && !on[WARM]) result &= uint16_t(~uint16_t(1));
    if (on[WARM] && !on[UV]) result &= uint16_t(~uint16_t(2));
    if (on[FANS]) result ^= kFanRelayMask;
    return result;
  }
};

// Relay commands need a physical release interval, including when the user
// explicitly selects OFF before requesting the other lamp. Call applied()
// only after a successful I2C write; an attempted write is not an ACK.
class RelayTransition {
 public:
  uint16_t next(uint16_t requested, uint32_t now) const {
    const uint16_t lamps = requested & 3U;
    if (lamps == 3U) return requested;
    if (lamps == 0U) return requested | 3U;
    if ((lastMask_ & 3U) != 3U && (lastMask_ & 3U) != lamps) {
      return requested | 3U;
    }
    if (released_ && uint32_t(now - releasedAt_) < 80U) {
      return requested | 3U;
    }
    return requested;
  }

  void applied(uint16_t mask, uint32_t now) {
    if ((lastMask_ & 3U) != 3U && (mask & 3U) == 3U) {
      released_ = true;
      releasedAt_ = now;
    } else if ((mask & 3U) != 3U) {
      released_ = false;
    }
    lastMask_ = mask;
  }

 private:
  uint16_t lastMask_ = kAllOffMask;
  uint32_t releasedAt_ = 0;
  bool released_ = false;
};

#endif
