// The sonos-button unit — headless. See plans/04-sonos-button-plan.md §5.
//
// core/unit.h is LVGL-free (just uiInit/uiTick), so a screenless unit is a first-class citizen
// and app.cpp needs no #ifdef for it. uiTick() runs at uiTask's ~5 ms cadence and IS the whole
// application: press the button -> the configured Sonos room starts the configured saved
// playlist, looped, at the configured volume. Press again -> stop.
//
// Named screens.cpp to match the other units' layout, even though there are no screens — the
// build_src_filter globs units/<unit>/ and consistency costs nothing.
#include "core/unit.h"
#include "core/board.h"
#include "core/settings.h"
#include "core/webconfig.h"
#include "core/player_state.h"
#include "core/library.h"
#include <Arduino.h>

// Enqueue + play round-trip over SOAP. The sleep-machine allows the same 20 s before giving up;
// matching it keeps the two units' failure behaviour identical.
static const uint32_t START_TIMEOUT_MS = 20000;

// Press-acknowledge pulse. The press-to-audio path is several SOAP calls; without instant local
// feedback a user wonders whether the press registered and mashes the button. So the ring gives
// a crisp ~110 ms transient the instant the button goes down — visible whether the ring is
// resting on or off — then returns to its configured level.
static const uint32_t PULSE_MS = 110;

enum class St {
  Idle,       // nothing playing (as far as we started it)
  Listing,    // browsing "SQ:" to publish the playlist names to the config page
  Starting,   // requestPlayNamed() posted; waiting for Sonos to reach Playing
  Playing,
};

static St       s_st        = St::Idle;
static uint32_t s_startMs   = 0;
static bool     s_listed    = false;   // the config page's playlist list has been published once
static uint32_t s_cfgGen    = 0;
static uint8_t  s_lastVol   = 0;       // last volume we pushed to Sonos; seeded in uiInit()
static uint32_t s_pulseEnd  = 0;       // ring-pulse deadline (0 = not pulsing)

// All ring writes funnel through here so the pulse and the resting level can't fight over the
// pin: while a pulse is active the tick restores the resting level when it expires.
static void ringRest() { backlightSet(settingsRing()); }

// Kick a press-acknowledge pulse. Drive the ring to the opposite extreme of where it's resting
// so the transient is visible either way: resting bright -> dip dark, resting dim/off -> flash
// bright. uiTick() restores the resting level when s_pulseEnd passes.
static void ringPulse() {
  backlightSet(settingsRing() >= 50 ? 0 : 100);
  s_pulseEnd = millis() + PULSE_MS;
}

// Headless — no screen, so nothing to show while the captive portal is up. The SoftAP itself is
// the whole setup UI (join it from a phone). appBoot() still calls this uniformly.
void uiProvisioning(const char * /*apSsid*/) {}

void uiInit() {
  // Apply the persisted ring level. Until this runs the ring is dark (boardInit leaves it off),
  // so a reboot never flashes the ring at full brightness in a dark room.
  s_cfgGen  = webConfigGen();
  s_lastVol = settingsVolume();   // seed, don't apply: a reboot must not shove the room's
                                  // volume around on its own
  ringRest();

  // Saved playlists are enqueued, so looping is REPEAT_ALL on the queue. Set once: this unit
  // has exactly one play mode and never wants a playlist to just stop at 3am.
  library::setLoopMode(true);

  Serial.printf("[unit   ] ring %u%%, playlist \"%s\" @ vol %u\n",
                settingsRing(), settingsPlaylist().c_str(), settingsVolume());
}

// Kick off "play the configured playlist". No browse on the hot path: requestPlayNamed() plays
// straight from the resolved-item cache (warmed at boot / on config change), so the press turns
// into just the enqueue+play SOAP calls. Volume goes first; netTask applies targetVolume to the
// SPEAKER while transport goes to the group COORDINATOR — app.cpp handles that split.
static void startPlaylist() {
  const uint8_t vol = settingsVolume();
  s_lastVol = vol;
  if (stateLock()) { g_pending.targetVolume = vol; stateUnlock(); }
  library::requestPlayNamed(settingsPlaylist());
  s_st      = St::Starting;
  s_startMs = millis();
  Serial.printf("[unit   ] start \"%s\" @ vol %u\n", settingsPlaylist().c_str(), vol);
}

static void stopPlayback() {
  if (stateLock()) { g_pending.setPlay = 0; stateUnlock(); }
  s_st = St::Idle;
  Serial.println("[unit   ] stop");
}

void uiTick() {
  const uint32_t now = millis();

  // --- ring press-pulse: restore the resting level when the transient expires ------------
  if (s_pulseEnd && (int32_t)(now - s_pulseEnd) >= 0) {
    s_pulseEnd = 0;
    ringRest();
  }

  // --- config page changed something we own -> re-read and apply -------------------------
  // A counter compare, not an NVS hit, per tick (webconfig.h explains why the unit applies the
  // ring rather than webConfigApply doing it centrally).
  const uint32_t gen = webConfigGen();
  if (gen != s_cfgGen) {
    s_cfgGen = gen;

    if (!s_pulseEnd) ringRest();   // don't stomp an in-flight pulse; it restores the new level

    // Push volume changes to Sonos immediately, not just at the next press. Reading it only in
    // startPlaylist() made the page's slider look broken: dragging it while playing did nothing.
    const uint8_t vol = settingsVolume();
    if (vol != s_lastVol) {
      s_lastVol = vol;
      if (stateLock()) { g_pending.targetVolume = vol; stateUnlock(); }
      Serial.printf("[unit   ] volume -> %u\n", vol);
    }

    // The playlist pick may have changed. Re-warm the cache for the current name so the next
    // press is fast (and a same-name delete/recreate re-resolves). warmOnly => resolve, no play.
    library::requestPlayNamed(settingsPlaylist(), /*warmOnly=*/true);
  }

  // --- snapshot the shared state once -----------------------------------------------------
  TransportState tr       = TransportState::Unknown;
  bool           haveZone = false;
  if (stateLock()) {
    tr       = g_player.transport;
    haveZone = g_player.coordinatorIp.length() > 0;
    stateUnlock();
  }

  // --- one-shot: publish the playlist names for the config page, then warm the play cache --
  // Lazy, not in uiInit(): appBoot() runs discovery AFTER uiInit(), so there's no zone to
  // browse yet at init time. Waits for a coordinator, and never competes with a real press.
  if (!s_listed && s_st == St::Idle && haveZone && !library::busy()) {
    library::requestBrowse("SQ:", library::PLAY_PLAYLIST);
    s_st      = St::Listing;
    s_startMs = now;            // Listing shares the same timeout; without this it fires at once
  }

  // --- the button -------------------------------------------------------------------------
  // Short is the toggle this product exists for. Long is reserved: §1 wants hold-at-boot for
  // the WiFi portal, which is a boot-time check, not a runtime event.
  const KnobEvent ev = knobEvent();
  if (ev == KnobEvent::Short) {
    ringPulse();   // acknowledge the press instantly, before any network work
    // Toggle on what Sonos is ACTUALLY doing, not just our own state — the room may have been
    // started or stopped from the Sonos app since we last looked. Starting counts as busy so a
    // double-tap can't fire two starts.
    if (s_st == St::Starting || s_st == St::Playing || tr == TransportState::Playing) stopPlayback();
    else                                                                              startPlaylist();
  } else if (ev == KnobEvent::Long) {
    Serial.println("[unit   ] button Long — reserved");
  }

  // --- state machine ----------------------------------------------------------------------
  switch (s_st) {
    case St::Listing: {
      std::vector<String> labels;
      if (library::takeResults(labels)) {
        webConfigPlaylistsSet(labels);
        s_listed = true;
        s_st     = St::Idle;
        Serial.printf("[unit   ] %u playlists published to the config page\n",
                      (unsigned)labels.size());
        // Pre-warm the resolved-item cache so the very first press is already fast.
        library::requestPlayNamed(settingsPlaylist(), /*warmOnly=*/true);
      } else if (now - s_startMs > START_TIMEOUT_MS) {
        s_listed = true;                 // don't retry forever; the page just shows a text box
        s_st     = St::Idle;
      }
      break;
    }

    case St::Starting:
      // requestPlayNamed() is doing the work on netTask. We just wait for the room to reach
      // Playing, surface a resolve failure, or time out.
      if (library::playNamedFailed()) {
        Serial.printf("[unit   ] playlist \"%s\" not found — set one on the config page\n",
                      settingsPlaylist().c_str());
        s_st = St::Idle;
      } else if (tr == TransportState::Playing) {
        s_st = St::Playing;
        Serial.println("[unit   ] playing");
      } else if (now - s_startMs > START_TIMEOUT_MS) {
        Serial.println("[unit   ] timed out starting playback");
        s_st = St::Idle;
      }
      break;

    case St::Playing:
      // Stopped from the Sonos app, or the queue ran out despite REPEAT_ALL.
      if (tr == TransportState::Stopped || tr == TransportState::Paused) {
        s_st = St::Idle;
        Serial.println("[unit   ] stopped externally");
      }
      break;

    case St::Idle:
    default:
      break;
  }
}
