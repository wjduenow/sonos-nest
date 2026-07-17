// Debounce + Short/Long classification for the FLM12-FJ-6 on PIN_BUTTON. See button.h.
//
// Thresholds are the ones measured by the Phase-0 bring-up (bringup.cpp) on the real button:
// ~2 raw edges per press, all absorbed by a 30 ms window, with no chatter at idle.
#include "button.h"
#include "pins.h"
#include <Arduino.h>

// 30 ms is the usual safe floor for a cheap momentary and is imperceptible. The bring-up
// measured ~2 edges/press against it with zero leakage, so there's no reason to go higher.
static const uint32_t DEBOUNCE_MS   = 30;
// Previews nothing measured yet — the bring-up never actually observed a held press, so this
// is still a guess. If Long ever feels wrong, measure it before tuning: bringup.cpp prints the
// held time on every release.
static const uint32_t LONG_PRESS_MS = 700;

static bool      s_raw        = false;   // last raw sample
static bool      s_stable     = false;   // debounced level
static uint32_t  s_lastChange = 0;
static uint32_t  s_pressedAt  = 0;
static bool      s_longFired  = false;   // Long already emitted for this hold
static KnobEvent s_queued     = KnobEvent::None;

static inline bool rawDown() { return digitalRead(PIN_BUTTON) == LOW; }   // active-low

void buttonInit() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);     // idle HIGH; the button shorts to GND
  s_raw = s_stable = rawDown();
  s_lastChange = millis();
  s_queued = KnobEvent::None;
}

// Sample + debounce. Called from buttonEvent()/buttonDown(), i.e. at uiTick's ~5 ms cadence —
// comfortably faster than the 30 ms window.
static void poll() {
  const uint32_t now = millis();
  const bool     raw = rawDown();

  if (raw != s_raw) { s_raw = raw; s_lastChange = now; }

  if (raw != s_stable && (now - s_lastChange) >= DEBOUNCE_MS) {
    s_stable = raw;
    if (s_stable) {
      s_pressedAt = now;
      s_longFired = false;
    } else if (!s_longFired) {
      // Short fires on RELEASE; a hold that already fired Long must not also fire Short.
      s_queued = KnobEvent::Short;
    }
  }

  // Long fires as soon as the threshold passes, without waiting for release — the contract
  // core/board.h documents.
  if (s_stable && !s_longFired && (now - s_pressedAt) >= LONG_PRESS_MS) {
    s_longFired = true;
    s_queued    = KnobEvent::Long;
  }
}

KnobEvent buttonEvent() {
  poll();
  KnobEvent e = s_queued;
  s_queued = KnobEvent::None;
  return e;
}

bool buttonDown() {
  poll();
  return s_stable;
}
