#include "../GreenhouseSimple/MenuInput.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint16_t raw(unsigned keys) {
  // Set unrelated inputs/outputs to prove they do not cause button events.
  return uint16_t(0xF0FF | ((keys & 0x0F) << 8));
}

static void idle(TouchButtons& buttons, uint32_t start = 0) {
  assert(buttons.poll(raw(0), start) == -1);
  assert(buttons.poll(raw(0), start + 40) == -1);
}

static void testSelectionAndActualActions() {
  MenuSelection menu;
  SimpleControl control;
  control.link(true);
  TouchButtons buttons;
  idle(buttons);

  // Real touch sequence: exclusive lamps, read-only soil, independent fans,
  // forward/backward wraps, turning a selected load off, and ALL OFF.
  struct Step { unsigned key; int page; unsigned states; };
  const Step steps[] = {
      {2, PAGE_WARM, 2},  // Enable WARM from the initial page.
      {1, PAGE_UV, 2},    // Navigation alone must not change loads.
      {2, PAGE_UV, 1},    // UV replaces WARM.
      {1, PAGE_SOIL, 1},
      {2, PAGE_SOIL, 1},  // Sensor activation cannot touch any output.
      {1, PAGE_FANS, 1},
      {2, PAGE_FANS, 5},
      {1, PAGE_WARM, 5},  // Wrap forward.
      {2, PAGE_WARM, 6},  // WARM replaces UV; fans remain on.
      {0, PAGE_FANS, 6},  // Wrap backward.
      {0, PAGE_SOIL, 6},
      {2, PAGE_SOIL, 6},  // Preserve both the lamp and fans on a sensor page.
      {0, PAGE_UV, 6},
      {2, PAGE_UV, 5},
      {3, PAGE_UV, 0},
      {0, PAGE_WARM, 0},
      {0, PAGE_FANS, 0},
      {2, PAGE_FANS, 4},
      {1, PAGE_WARM, 4},
      {2, PAGE_WARM, 6},
      {2, PAGE_WARM, 4},  // Turn WARM off without stopping fans.
      {3, PAGE_WARM, 0}
  };
  uint32_t now = 100;
  for (const Step& step : steps) {
    const uint16_t previousMask = control.mask();
    assert(buttons.poll(raw(1U << step.key), now) == -1);
    const int event = buttons.poll(raw(1U << step.key), now + 40);
    assert(event == int(step.key));
    switch (event) {
      case 0: menu.previous(); break;
      case 1: menu.next(); break;
      case 2:
        if (menu.channel() >= 0) assert(control.toggle(menu.channel()));
        break;
      case 3: control.allOff(); break;
    }
    assert(menu.selected == step.page);
    if (event == 0 || event == 1 || (event == 2 && menu.selected == PAGE_SOIL))
      assert(control.mask() == previousMask);
    unsigned actual = 0;
    for (unsigned channel = 0; channel < 3; ++channel) {
      if (control.on[channel]) actual |= 1U << channel;
    }
    assert(actual == step.states);
    assert(!(control.on[UV] && control.on[WARM]));
    assert(buttons.poll(raw(0), now + 50) == -1);
    assert(buttons.poll(raw(0), now + 90) == -1);
    now += 100;
  }
}

static void testPageOrderAndChannelMapping() {
  MenuSelection menu;
  assert(menu.selected == PAGE_WARM);
  const int clockwise[] = {PAGE_WARM, PAGE_UV, PAGE_SOIL, PAGE_FANS};
  const int channels[] = {WARM, UV, -1, FANS};
  for (unsigned lap = 0; lap < 3; ++lap) {
    for (unsigned page = 0; page < 4; ++page) {
      assert(menu.selected == clockwise[page]);
      assert(menu.channel() == channels[page]);
      menu.next();
    }
    assert(menu.selected == PAGE_WARM);
  }
  const int counterclockwise[] = {PAGE_FANS, PAGE_SOIL, PAGE_UV, PAGE_WARM};
  for (unsigned lap = 0; lap < 3; ++lap) {
    for (int page : counterclockwise) {
      menu.previous();
      assert(menu.selected == page);
      assert(menu.channel() == channels[page]);
    }
  }
  // An invalid externally assigned page must never address a load.
  menu.selected = -1;
  assert(menu.channel() == -1);
  menu.selected = PAGE_COUNT;
  assert(menu.channel() == -1);
}

static void testDebounceHeldAndReleaseBounce() {
  TouchButtons buttons;
  idle(buttons);
  assert(buttons.poll(raw(4), 100) == -1);
  assert(buttons.poll(raw(0), 110) == -1);
  assert(buttons.poll(raw(4), 120) == -1);
  assert(buttons.poll(raw(4), 159) == -1);
  assert(buttons.poll(raw(4), 160) == 2);
  assert(buttons.poll(raw(4), 1000) == -1);
  // A short LOW release and a return to HIGH cannot create a second action.
  assert(buttons.poll(raw(0), 1010) == -1);
  assert(buttons.poll(raw(4), 1049) == -1);
  assert(buttons.poll(raw(4), 1100) == -1);
  assert(buttons.poll(raw(0), 1110) == -1);
  assert(buttons.poll(raw(0), 1150) == -1);
  assert(buttons.poll(raw(4), 1160) == -1);
  assert(buttons.poll(raw(4), 1200) == 2);
}

static void testStartupAndReconnectHeld() {
  for (unsigned key = 0; key < 4; ++key) {
    TouchButtons buttons;
    const unsigned held = 1U << key;
    assert(buttons.poll(raw(held), 0) == -1);
    assert(buttons.poll(raw(held), 40) == -1);
    assert(buttons.poll(raw(held), 1000) == -1);
    assert(buttons.poll(raw(0), 1010) == -1);
    assert(buttons.poll(raw(0), 1050) == -1);
    assert(buttons.poll(raw(held), 1060) == -1);
    assert(buttons.poll(raw(held), 1100) == int(key));
    buttons.reset();
    assert(buttons.poll(raw(held), 2000) == -1);
    assert(buttons.poll(raw(held), 2040) == -1);
    assert(buttons.poll(raw(0), 2050) == -1);
    assert(buttons.poll(raw(0), 2090) == -1);
    assert(buttons.poll(raw(held), 2100) == -1);
    assert(buttons.poll(raw(held), 2140) == int(key));
  }
}

static void testAllOffPriorityAndNoQueuedActivation() {
  TouchButtons buttons;
  idle(buttons);
  assert(buttons.poll(raw(15), 100) == -1);
  assert(buttons.poll(raw(15), 140) == 3);
  assert(buttons.poll(raw(15), 1000) == -1);
  // Release ALL OFF while the action keys remain held: no action is queued.
  assert(buttons.poll(raw(7), 1010) == -1);
  assert(buttons.poll(raw(7), 1050) == -1);
  assert(buttons.poll(raw(7), 1090) == -1);
  assert(buttons.poll(raw(0), 1100) == -1);
  assert(buttons.poll(raw(0), 1140) == -1);

  // ALL OFF begins just before activation matures. Activation is suppressed.
  assert(buttons.poll(raw(4), 1200) == -1);
  assert(buttons.poll(raw(12), 1239) == -1);
  assert(buttons.poll(raw(12), 1240) == -1);
  assert(buttons.poll(raw(12), 1279) == 3);
  assert(buttons.poll(raw(4), 1300) == -1);
  assert(buttons.poll(raw(4), 1340) == -1);
  assert(buttons.poll(raw(4), 1400) == -1);
}

static void testAmbiguousChordsAndUnrelatedBits() {
  TouchButtons buttons;
  idle(buttons);
  assert(buttons.poll(raw(6), 100) == -1);  // NEXT + ACTIVATE.
  assert(buttons.poll(raw(6), 140) == -1);
  assert(buttons.poll(raw(4), 150) == -1);
  assert(buttons.poll(raw(4), 190) == -1);
  assert(buttons.poll(raw(4), 1000) == -1);
  assert(buttons.poll(raw(0), 1010) == -1);
  assert(buttons.poll(raw(0), 1050) == -1);
  for (unsigned bits = 0; bits <= UINT16_MAX; ++bits) {
    if ((bits & 0x0F00) == 0) {
      assert(buttons.poll(uint16_t(bits), 1100) == -1);
    }
  }
}

static void testRolloverAndIndependentKeys() {
  TouchButtons buttons;
  idle(buttons, UINT32_MAX - 100U);
  assert(buttons.poll(raw(1), UINT32_MAX - 20U) == -1);
  assert(buttons.poll(raw(1), 18) == -1);  // 39 ms.
  assert(buttons.poll(raw(1), 19) == 0);   // 40 ms.
  // Holding PREVIOUS does not stop a distinct NEXT press.
  assert(buttons.poll(raw(3), 30) == -1);
  assert(buttons.poll(raw(3), 70) == 1);
  assert(buttons.poll(raw(3), 80) == -1);
}

int main() {
  testSelectionAndActualActions();
  testPageOrderAndChannelMapping();
  testDebounceHeldAndReleaseBounce();
  testStartupAndReconnectHeld();
  testAllOffPriorityAndNoQueuedActivation();
  testAmbiguousChordsAndUnrelatedBits();
  testRolloverAndIndependentKeys();
  puts("All menu and touch-button tests passed.");
}
