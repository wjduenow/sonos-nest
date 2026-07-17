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

// A browse + enqueue + play round-trip over SOAP. The sleep-machine allows the same 20 s before
// giving up on its Bedtime path; matching it keeps the two units' failure behaviour identical.
static const uint32_t START_TIMEOUT_MS = 20000;

enum class St {
  Idle,       // nothing playing (as far as we started it)
  Listing,    // browsing "SQ:" to publish the playlist names to the config page
  Starting,   // browsing "SQ:" to find + play the configured playlist
  Playing,
};

static St       s_st      = St::Idle;
static uint32_t s_startMs = 0;
static bool     s_played  = false;   // requestPlay() already issued for this Starting pass
static bool     s_listed  = false;   // the config page's playlist list has been published once
static uint32_t s_cfgGen  = 0;
static uint8_t  s_lastVol = 0;       // last volume we pushed to Sonos; seeded in uiInit()

void uiInit() {
  // Apply the persisted ring level. Until this runs the ring is dark (boardInit leaves it off),
  // so a reboot never flashes the ring at full brightness in a dark room.
  s_cfgGen  = webConfigGen();
  s_lastVol = settingsVolume();   // seed, don't apply: a reboot must not shove the room's
                                  // volume around on its own
  backlightSet(settingsRing());

  // Saved playlists are enqueued, so looping is REPEAT_ALL on the queue. Set once: this unit
  // has exactly one play mode and never wants a playlist to just stop at 3am.
  library::setLoopMode(true);

  Serial.printf("[unit   ] ring %u%%, playlist \"%s\" @ vol %u\n",
                settingsRing(), settingsPlaylist().c_str(), settingsVolume());
}

// Kick off "play the configured playlist": set the volume first, then browse. netTask applies
// targetVolume to the SPEAKER while transport goes to the group COORDINATOR — app.cpp already
// handles that split, so don't re-derive it here.
static void startPlaylist() {
  // Still set it here as well as on change: the room may have been turned up from the Sonos app
  // since we last pushed, and the whole point is that the button plays at a KNOWN volume.
  const uint8_t vol = settingsVolume();
  s_lastVol = vol;
  if (stateLock()) { g_pending.targetVolume = vol; stateUnlock(); }
  library::requestBrowse("SQ:", library::PLAY_PLAYLIST);   // clears queue, enqueues, plays
  s_st      = St::Starting;
  s_startMs = millis();
  s_played  = false;
  Serial.printf("[unit   ] start \"%s\" @ vol %u\n", settingsPlaylist().c_str(), vol);
}

static void stopPlayback() {
  if (stateLock()) { g_pending.setPlay = 0; stateUnlock(); }
  s_st = St::Idle;
  Serial.println("[unit   ] stop");
}

void uiTick() {
  // --- config page changed something we own -> re-read and apply -------------------------
  // A counter compare, not an NVS hit, per tick (webconfig.h explains why the unit applies the
  // ring rather than webConfigApply doing it centrally).
  const uint32_t gen = webConfigGen();
  if (gen != s_cfgGen) {
    s_cfgGen = gen;

    const uint8_t pct = settingsRing();
    backlightSet(pct);

    // Push volume changes to Sonos immediately, not just at the next press. Reading it only in
    // startPlaylist() made the page's slider look broken: you drag it while the playlist is
    // playing and nothing happens, because the value silently waits for a press that may be
    // hours away. A slider labelled "Volume" has to move the volume.
    const uint8_t vol = settingsVolume();
    if (vol != s_lastVol) {
      s_lastVol = vol;
      if (stateLock()) { g_pending.targetVolume = vol; stateUnlock(); }
      Serial.printf("[unit   ] volume -> %u\n", vol);
    }
  }

  // --- snapshot the shared state once -----------------------------------------------------
  TransportState tr     = TransportState::Unknown;
  bool           haveZone = false;
  if (stateLock()) {
    tr       = g_player.transport;
    haveZone = g_player.coordinatorIp.length() > 0;
    stateUnlock();
  }

  const uint32_t now = millis();

  // --- one-shot: publish the playlist names for the config page ---------------------------
  // Lazy, not in uiInit(): appBoot() runs discovery AFTER uiInit(), so there's no zone to
  // browse yet at init time. Waits for a coordinator, and never competes with a real press
  // (Idle only).
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
    // Toggle on what Sonos is ACTUALLY doing, not just our own state — the room may have been
    // started or stopped from the Sonos app since we last looked, and the button should still
    // feel right. Starting counts as "busy" so a double-tap can't fire two browses.
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
      } else if (now - s_startMs > START_TIMEOUT_MS) {
        s_listed = true;                 // don't retry forever; the page just shows a text box
        s_st     = St::Idle;
      }
      break;
    }

    case St::Starting: {
      if (!s_played) {
        std::vector<String> labels;
        if (library::takeResults(labels)) {
          webConfigPlaylistsSet(labels);   // free refresh — we browsed anyway
          s_listed = true;
          const String want = settingsPlaylist();
          int idx = -1;
          for (int i = 0; i < (int)labels.size(); ++i) if (labels[i] == want) { idx = i; break; }
          if (idx >= 0) {
            library::requestPlay(idx);
            s_played = true;
          } else {
            // Say which one is missing AND what does exist — on a screenless box the serial log
            // is the only place this can ever be explained.
            Serial.printf("[unit   ] playlist \"%s\" not found. Available:\n", want.c_str());
            for (const String &l : labels) Serial.printf("[unit   ]   - %s\n", l.c_str());
            s_st = St::Idle;
          }
        }
      }
      if (tr == TransportState::Playing) {
        s_st = St::Playing;
        Serial.println("[unit   ] playing");
      } else if (now - s_startMs > START_TIMEOUT_MS) {
        Serial.println("[unit   ] timed out starting playback");
        s_st = St::Idle;
      }
      break;
    }

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
