#include "../GreenhouseSimple/SimpleControl.h"

#include <assert.h>
#include <stdio.h>

static void invariant(const SimpleControl& control) {
  const uint16_t mask = control.mask();
  assert((mask & 0x003C) == 0);       // All old motor-driver pins stay low.
  assert((mask & 0x0F00) == 0x0F00);  // All four button inputs released.
  assert((mask & 0xD0C0) == 0xD0C0);  // Unused PCF pins released.
  assert((mask & 0x0003) != 0);       // Never energize both lamps.
  if (!control.ioReady) assert(mask == kAllOffMask);
}

static void assertOff(const SimpleControl& control) {
  assert(!control.on[UV] && !control.on[WARM] && !control.on[FANS]);
  assert(control.mask() == kAllOffMask);
  invariant(control);
}

static void testBootAndMissingIO() {
  static_assert(kFanRelayBit == 13, "PCF P15 must use bit 13");
  static_assert(kFanRelayMask == 0x2000, "Fan relay mask must be 0x2000");
  static_assert(!kFanRelayActiveLow, "Tests expect the active-high relay wiring");
  static_assert(kAllOffMask == 0xDFC3, "Active-high fan must be LOW when off");
  SimpleControl control;
  assert(!control.ioReady);
  assertOff(control);
  for (int channel = -2; channel <= 4; ++channel) {
    assert(!control.toggle(channel));
    assertOff(control);
  }
  control.link(true);
  assert(control.ioReady);
  assertOff(control);
  assert(!control.toggle(-1));
  assert(!control.toggle(3));
  assertOff(control);
}

static void testLightAndFanTransitions() {
  SimpleControl control;
  control.link(true);
  assert(control.toggle(UV));
  assert(control.on[UV] && !control.on[WARM]);
  assert(control.mask() == 0xDFC2);
  invariant(control);

  assert(control.toggle(FANS));
  assert(control.mask() == 0xFFC2);
  assert(control.on[FANS]);
  invariant(control);

  assert(control.toggle(WARM));
  assert(!control.on[UV] && control.on[WARM] && control.on[FANS]);
  assert(control.mask() == 0xFFC1);
  invariant(control);

  assert(control.toggle(UV));
  assert(control.on[UV] && !control.on[WARM] && control.on[FANS]);
  assert(control.mask() == 0xFFC2);
  invariant(control);

  assert(control.toggle(UV));
  assert(!control.on[UV] && !control.on[WARM] && control.on[FANS]);
  assert(control.mask() == 0xFFC3);
  invariant(control);

  assert(control.toggle(FANS));
  assertOff(control);
  assert(control.toggle(WARM));
  assert(control.mask() == 0xDFC1);
  assert(control.toggle(WARM));
  assertOff(control);
}

static void testFailureAndRecovery() {
  SimpleControl control;
  control.link(true);
  assert(control.toggle(WARM));
  assert(control.toggle(FANS));
  control.link(false);
  assert(!control.ioReady);
  assertOff(control);
  assert(!control.toggle(UV));
  assert(!control.toggle(FANS));
  control.link(true);
  assertOff(control);  // Recovery must never restart previous loads.
  assert(control.toggle(UV));
  assert(control.toggle(FANS));
  control.allOff();
  assert(control.ioReady);
  assertOff(control);
}

static void testAllMasksIncludingInvalidPublicStates() {
  const uint16_t expected[8] = {
      0xDFC3, 0xDFC2, 0xDFC1, 0xDFC3,
      0xFFC3, 0xFFC2, 0xFFC1, 0xFFC3};
  for (unsigned states = 0; states < 8; ++states) {
    SimpleControl control;
    control.link(true);
    for (unsigned channel = 0; channel < 3; ++channel) {
      control.on[channel] = (states & (1U << channel)) != 0;
    }
    assert(control.mask() == expected[states]);
    invariant(control);
    // Even direct edits cannot generate an ON mask without a working link.
    control.ioReady = false;
    assert(control.mask() == kAllOffMask);
    invariant(control);
  }
}

static void testFanRelayChangesOnlyP15() {
  for (int lamp = -1; lamp <= WARM; ++lamp) {
    SimpleControl control;
    control.link(true);
    if (lamp >= 0) assert(control.toggle(lamp));
    const uint16_t before = control.mask();
    assert(control.toggle(FANS));
    const uint16_t enabled = control.mask();
    assert((before ^ enabled) == 0x2000);
    assert((before & 0x2000) == 0);
    assert((enabled & 0x2000) != 0);
    invariant(control);
    assert(control.toggle(FANS));
    assert(control.mask() == before);
    invariant(control);
  }
}

static void testDirectLampTransition() {
  const uint16_t firstModes[] = {0xDFC2, 0xDFC1};
  for (uint16_t first : firstModes) {
    const uint16_t second = first ^ 3U;
    RelayTransition relay;
    assert(relay.next(first, 100) == first);
    relay.applied(first, 100);
    assert(relay.next(first, 101) == first);
    assert(relay.next(second, 120) == kAllOffMask);
    // No ACK for the OFF attempt: the waiting period has not started.
    assert(relay.next(second, 1000) == kAllOffMask);
    relay.applied(kAllOffMask, 1000);
    assert(relay.next(second, 1079) == kAllOffMask);
    assert(relay.next(second, 1080) == second);
    relay.applied(second, 1080);
    assert(relay.next(second, 1081) == second);
  }
}

static void testExplicitOffAndRapidChanges() {
  RelayTransition relay;
  relay.applied(0xDFC2, 100);
  relay.applied(kAllOffMask, 200);  // User presses ALL OFF while UV was on.
  assert(relay.next(0xDFC1, 201) == kAllOffMask);
  assert(relay.next(0xFFC1, 220) == 0xFFC3);  // Fan relay unaffected by delay.
  relay.applied(0xFFC3, 220);
  assert(relay.next(0xDFC2, 250) == kAllOffMask);  // Return to original lamp.
  relay.applied(kAllOffMask, 250);
  assert(relay.next(0xDFC1, 279) == kAllOffMask);
  assert(relay.next(0xDFC1, 280) == 0xDFC1);  // Repeated OFF did not reset time.
  relay.applied(0xDFC1, 280);
  assert(relay.next(kAllOffMask, 290) == kAllOffMask);  // OFF is immediate.
  relay.applied(kAllOffMask, 290);
  assert(relay.next(0xDFC1, 369) == kAllOffMask);
  assert(relay.next(0xDFC1, 370) == 0xDFC1);
}

static void testRelayTimerWrapAndOutputPreservation() {
  RelayTransition relay;
  relay.applied(0xDFC1, UINT32_MAX - 50U);
  relay.applied(kAllOffMask, UINT32_MAX - 20U);
  assert(relay.next(0xDFC2, 10) == kAllOffMask);
  assert(relay.next(0xDFC2, 58) == kAllOffMask);
  assert(relay.next(0xDFC2, 59) == 0xDFC2);
  for (unsigned requested = 0; requested <= UINT16_MAX; ++requested) {
    const uint16_t actual = relay.next(uint16_t(requested), 10);
    assert((actual & uint16_t(~3U)) == (requested & uint16_t(~3U)));
    assert((actual & 3U) == 3U);
  }
}

// Traverse every sequence of 7 actions, including failures and reconnects.
static void explore(SimpleControl control, unsigned depth) {
  invariant(control);
  assert(!(control.on[UV] && control.on[WARM]));
  if (!control.ioReady) assertOff(control);
  if (!depth) return;
  for (unsigned action = 0; action < 7; ++action) {
    SimpleControl next = control;
    switch (action) {
      case 0:
      case 1:
      case 2:
        assert(next.toggle(int(action)) == next.ioReady);
        break;
      case 3: next.link(false); break;
      case 4: next.link(true); break;
      case 5: next.allOff(); break;
      case 6: assert(!next.toggle(100)); break;
    }
    explore(next, depth - 1);
  }
}

int main() {
  testBootAndMissingIO();
  testLightAndFanTransitions();
  testFailureAndRecovery();
  testAllMasksIncludingInvalidPublicStates();
  testFanRelayChangesOnlyP15();
  testDirectLampTransition();
  testExplicitOffAndRapidChanges();
  testRelayTimerWrapAndOutputPreservation();
  explore(SimpleControl(), 7);
  puts("PASS: boot, exclusive lights, fan, I/O recovery, all masks, relay "
       "release delay including OFF, uint32 wrap and 823543 action sequences.");
  return 0;
}
