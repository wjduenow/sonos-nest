// Debounce + Short/Double/Triple/Long classification for the FLM12-FJ-6. See button.h.
//
// Thresholds are the ones measured by the Phase-0 bring-up (boards/esp32s3cam/bringup.cpp) on the
// real button: ~2 raw edges per press, all absorbed by a 30 ms window, with no chatter at idle.
// They are switch properties, not board properties, so both button boards share them.
#include "button.h"
#include <Arduino.h>

// 30 ms is the usual safe floor for a cheap momentary and is imperceptible. The bring-up
// measured ~2 edges/press against it with zero leakage, so there's no reason to go higher.
static const uint32_t DEBOUNCE_MS   = 30;
// Previews nothing measured yet — the bring-up never actually observed a held press, so this
// is still a guess. If Long ever feels wrong, measure it before tuning: bringup.cpp prints the
// held time on every release.
static const uint32_t LONG_PRESS_MS = 700;
// How long a release waits for the NEXT press before it is finally called a single press.
// This is the whole cost of double/triple press: it is added to every single press, because
// "one press" is only knowable once the window has passed without a second one. 350 ms is the
// usual comfortable ceiling for a deliberate double-tap while still feeling prompt; the press
// itself is acknowledged instantly by the unit's ring pulse, which runs off knobDown().
static const uint32_t MULTI_GAP_MS  = 350;
static const uint8_t  MAX_CLICKS    = 3;   // Triple is the deepest press this HAL classifies

static uint8_t   s_pin        = 0xFF;    // set by buttonInit(); 0xFF = never initialised
static bool      s_raw        = false;   // last raw sample
static bool      s_stable     = false;   // debounced level
static uint32_t  s_lastChange = 0;
static uint32_t  s_pressedAt  = 0;
static bool      s_longFired  = false;   // Long already emitted for this hold
static uint8_t   s_clicks     = 0;       // releases so far in the current multi-press window
static uint32_t  s_lastRelease= 0;       // when the most recent one landed
static KnobEvent s_queued     = KnobEvent::None;

// Active-low. Before buttonInit() there is no pin to read, and reading GPIO 0xFF would be a
// wild access — report "up", which is what an unwired button looks like anyway.
static inline bool rawDown() { return s_pin != 0xFF && digitalRead(s_pin) == LOW; }

void buttonInit(uint8_t pin) {
  s_pin = pin;
  pinMode(s_pin, INPUT_PULLUP);          // idle HIGH; the button shorts to GND
  s_raw = s_stable = rawDown();
  s_lastChange = millis();
  s_clicks = 0;
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
      // A release ENDS a click but no longer decides what the gesture was — that needs the gap
      // below. A hold that already fired Long must not count as a click at all.
      s_lastRelease = now;
      if (++s_clicks >= MAX_CLICKS) {
        // Deepest gesture we classify: emit it on the release itself rather than making the user
        // wait out a window for a count that cannot grow.
        s_queued = KnobEvent::Triple;
        s_clicks = 0;
      }
    }
  }

  // Long fires as soon as the threshold passes, without waiting for release — the contract
  // core/board.h documents. It also abandons any clicks banked before the hold: a press-press-HOLD
  // is a hold, not a double press with a hold stapled on.
  if (s_stable && !s_longFired && (now - s_pressedAt) >= LONG_PRESS_MS) {
    s_longFired = true;
    s_clicks    = 0;
    s_queued    = KnobEvent::Long;
  }

  // The multi-press window closed with the button up: whatever was banked is the gesture.
  if (s_clicks && !s_stable && (now - s_lastRelease) >= MULTI_GAP_MS) {
    s_queued = (s_clicks == 1) ? KnobEvent::Short : KnobEvent::Double;
    s_clicks = 0;
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
