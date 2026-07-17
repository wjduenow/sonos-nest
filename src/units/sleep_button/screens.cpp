// The sonos-button unit — headless. See plans/04-sonos-button-plan.md §5.
//
// core/unit.h is LVGL-free (just uiInit/uiTick), so a screenless unit is a first-class citizen
// and app.cpp needs no #ifdef for it. uiInit() sets the ring; uiTick() runs at uiTask's ~5 ms
// cadence and is the entire application.
//
// Named screens.cpp to match the other units' layout, even though there are no screens — the
// build_src_filter globs units/<unit>/ and consistency costs nothing.
#include "core/unit.h"
#include "core/board.h"
#include "core/settings.h"
#include "core/webconfig.h"
#include <Arduino.h>

static uint32_t s_cfgGen = 0;

void uiInit() {
  // Apply the persisted ring level. Until this runs the ring is dark (boardInit leaves it off),
  // so a reboot never flashes the ring at full brightness in a dark room.
  s_cfgGen = webConfigGen();
  backlightSet(settingsRing());
  Serial.printf("[unit   ] ring restored to %u%%\n", settingsRing());
}

void uiTick() {
  // The config page changed something we own -> re-read and apply. A counter compare, not an
  // NVS hit, per tick (webconfig.h explains why the unit does this rather than webConfigApply).
  const uint32_t gen = webConfigGen();
  if (gen != s_cfgGen) {
    s_cfgGen = gen;
    const uint8_t pct = settingsRing();
    backlightSet(pct);
    Serial.printf("[unit   ] ring -> %u%%\n", pct);
  }

  // The button. Short = the toggle this product exists for; Long is reserved (§1 wants
  // hold-at-boot for the WiFi portal, which is a different, boot-time check).
  switch (knobEvent()) {
    case KnobEvent::Short:
      // TODO(next increment): toggle the Sonos sleep playlist — SetVolume + browse "SQ:",
      // match by name, requestPlay(); press again -> stop. Near-copy of the sleep-machine's
      // Bedtime path (units/sleep_machine/screens.cpp). Deliberately not stubbed with a fake
      // action: a button that lies about what it did is worse than one that says it isn't
      // wired yet.
      Serial.println("[unit   ] button Short — Sonos toggle not wired yet (next increment)");
      break;
    case KnobEvent::Long:
      Serial.println("[unit   ] button Long — reserved");
      break;
    case KnobEvent::None:
    default:
      break;
  }
}
