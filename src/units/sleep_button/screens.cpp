// The sonos-button unit — headless. See plans/04-sonos-button-plan.md §5.
//
// core/unit.h is LVGL-free (just uiInit/uiTick), so a screenless unit is a first-class citizen
// and app.cpp needs no #ifdef for it. uiTick() runs at uiTask's ~5 ms cadence and IS the whole
// application: press the button -> the configured Sonos room starts the configured saved
// playlist, looped, at the configured volume. Press again -> stop.
//
// A double and a triple press start their OWN configured playlist and volume (settings.h press
// slots; both default to unmapped, so a device nobody has configured behaves exactly as before).
// Those two always START — they switch the room onto their playlist whatever it is doing. Only the
// single press stops, deliberately: with three gestures that can start something, "which press
// stops it?" has to have one answer, and the one press everybody already knows is it.
//
// Named screens.cpp to match the other units' layout, even though there are no screens — the
// build_src_filter globs units/<unit>/ and consistency costs nothing.
#include "core/unit.h"
#include "core/board.h"
#include "core/settings.h"
#include "core/webconfig.h"
#include "core/player_state.h"
#include "core/library.h"
#include "core/app.h"             // g_link* — netTask's published link snapshot, for the heartbeat
#include <Arduino.h>
#include "core/net/logmirror.h"   // LOG — tees to the TCP mirror where enabled, plain Serial otherwise

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
static uint32_t s_plGen     = 0;       // playlist-pick generation; see the re-warm in uiTick
static uint8_t  s_lastVol   = 0;       // last volume we pushed to Sonos; seeded in uiInit()
static uint32_t s_pulseEnd  = 0;       // ring-pulse deadline (0 = not pulsing)
static bool     s_wasDown   = false;   // previous knobDown(), for the press-edge pulse
static uint8_t  s_slot      = 1;       // press slot whose playlist is starting/playing

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
  // Serial log over TCP. This unit is HEADLESS — no screen, one button — so without a cable there
  // is no way to see what it is doing at all, which makes it the unit that benefits most. It also
  // has by far the most room: ~243 KB free / 226 KB minimum heap, against the sleep-machine's
  // 14.5 KB. No-op without -DLOG_MIRROR; the task waits for Wi-Fi itself.
  logMirrorBegin();

  // Apply the persisted ring level. Until this runs the ring is dark (boardInit leaves it off),
  // so a reboot never flashes the ring at full brightness in a dark room.
  s_cfgGen  = webConfigGen();
  s_plGen   = webConfigPlaylistGen();
  s_lastVol = settingsVolume(1);  // seed, don't apply: a reboot must not shove the room's
                                  // volume around on its own
  ringRest();

  // Saved playlists are enqueued, so looping is REPEAT_ALL on the queue. Set once: this unit
  // has exactly one play mode and never wants a playlist to just stop at 3am.
  library::setLoopMode(true);

  for (uint8_t s = 1; s <= SETTINGS_PRESS_SLOTS; ++s) {
    const String name  = settingsPlaylist(s);
    const String label = name.length() ? "\"" + name + "\"" : String("(unmapped)");
    LOG.printf("[unit   ] press x%u: %s @ vol %u\n", s, label.c_str(), settingsVolume(s));
  }
  LOG.printf("[unit   ] ring %u%%\n", settingsRing());
}

// Kick off "play the configured playlist". No browse on the hot path: requestPlayNamed() plays
// straight from the resolved-item cache (warmed at boot / on config change), so the press turns
// into just the enqueue+play SOAP calls. Volume goes first; netTask applies targetVolume to the
// SPEAKER while transport goes to the group COORDINATOR — app.cpp handles that split.
static void startPlaylist(uint8_t slot) {
  const String name = settingsPlaylist(slot);
  if (name.length() == 0) {
    // An unmapped double/triple press. Do nothing at all rather than falling back to slot 1: a
    // press that quietly starts something you didn't map is worse than a press that does nothing.
    LOG.printf("[unit   ] press x%u — unmapped, ignored\n", slot);
    return;
  }
  const uint8_t vol = settingsVolume(slot);
  s_lastVol = vol;
  if (stateLock()) { g_pending.targetVolume = vol; stateUnlock(); }
  library::requestPlayNamed(name);
  s_slot    = slot;
  s_st      = St::Starting;
  s_startMs = millis();
  LOG.printf("[unit   ] press x%u start \"%s\" @ vol %u\n", slot, name.c_str(), vol);
}

static void stopPlayback() {
  if (stateLock()) { g_pending.setPlay = 0; stateUnlock(); }
  s_st = St::Idle;
  LOG.println("[unit   ] stop");
}

void uiTick() {
  const uint32_t now = millis();

  // --- heartbeat -------------------------------------------------------------------------
  // This unit is HEADLESS and otherwise only logs on events, so an idle button produced a log
  // mirror that connected and then said nothing at all — indistinguishable from a wedged device.
  // For a screenless unit "is it alive, on Wi-Fi, and pointed at a room" IS the diagnostic, and
  // this is the only place it can be answered from. Every 30 s: cheap, and rare enough that the
  // 8 KB ring holds hours of it.
  //
  // Deliberately reads only netTask's published snapshot (g_link*, core/app.h) and the local
  // state machine — never WiFi.* or sonos::, which are blocking calls that would stall uiTask
  // in exactly the fault this exists to report.
  {
    static uint32_t lastBeat = 0;
    if (now - lastBeat >= 30000) {
      lastBeat = now;
      String room;
      if (stateLock()) { room = g_player.zoneName; stateUnlock(); }
      const uint32_t ip = g_linkIp;
      LOG.printf("[health ] up=%lus heap=%luKB min=%luKB wifi=%d rssi=%d ip=%u.%u.%u.%u "
                 "zones=%u room=%s state=%d\n",
                 (unsigned long)(now / 1000),
                 (unsigned long)(ESP.getFreeHeap() / 1024),
                 (unsigned long)(ESP.getMinFreeHeap() / 1024),
                 g_linkStatus, g_linkRssi,
                 (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                 (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF),
                 (unsigned)g_linkZones, room.length() ? room.c_str() : "-", (int)s_st);
    }
  }

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
    // It's the ACTIVE slot's volume that's live — dragging the double-press slider while the
    // single-press playlist plays must not move the room, or the two sliders fight.
    const uint8_t vol = settingsVolume(s_slot);
    if (vol != s_lastVol) {
      s_lastVol = vol;
      if (stateLock()) { g_pending.targetVolume = vol; stateUnlock(); }
      LOG.printf("[unit   ] volume -> %u (slot %u)\n", vol, s_slot);
    }

    // Re-warm every mapped slot so the next press is fast whichever one it is. warmOnly =>
    // resolve, no play; requestPlayNamed ignores an unmapped "" and queues the rest one browse per
    // netTask pass, so this costs nothing on the press path.
    //
    // FORCE the re-resolve only when a playlist pick was what changed. A cache hit is normally the
    // right answer and is free, but it is exactly wrong after a playlist was deleted and recreated
    // under the same name: the title matches, the res URI does not, and the press would enqueue a
    // URI that no longer exists and silently do nothing. Gating on the playlist-specific generation
    // is what keeps that browse off a volume drag, which bumps the general counter continuously.
    const uint32_t plGen = webConfigPlaylistGen();
    const bool     force = (plGen != s_plGen);
    s_plGen = plGen;
    for (uint8_t s = 1; s <= SETTINGS_PRESS_SLOTS; ++s)
      library::requestPlayNamed(settingsPlaylist(s), /*warmOnly=*/true, force);
  }

  // --- snapshot the shared state once -----------------------------------------------------
  TransportState tr       = TransportState::Unknown;
  bool           haveZone = false;
  String         srcUri;
  if (stateLock()) {
    tr       = g_player.transport;
    srcUri   = g_player.currentUri;
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
  // Short is the toggle this product exists for; Double/Triple start their own press slot. Long is
  // reserved: §1 wants hold-at-boot for the WiFi portal, which is a boot-time check, not a runtime
  // event.
  //
  // The pulse now rides the press EDGE, not the Short event. Classifying multi-presses means Short
  // can only fire once the multi-press window has closed (~350 ms after release), and feedback that
  // arrives a third of a second after your finger reads as a missed press — the exact thing the
  // pulse was added to prevent. knobDown() is still edge-immediate, and pulsing per press also
  // makes a double or triple press countable in the dark.
  const bool down = knobDown();
  if (down && !s_wasDown) ringPulse();
  s_wasDown = down;

  const KnobEvent ev = knobEvent();
  if (ev == KnobEvent::Short) {
    // Stop when the room is actively playing FROM ITS QUEUE — that's what this button starts, so a
    // press should stop it whether WE started it, the Sonos app did, or we rebooted mid-play (s_st
    // is then Idle but the queue keeps going — the reported bug where a press re-enqueued instead of
    // stopping). St::Starting also counts as busy so a double-tap can't fire two starts. Otherwise
    // start/override: a soundbar plays TV/line-in as a DIFFERENT source (x-sonos-htastream /
    // x-rincon-stream), so a press there takes the room onto the sleep playlist rather than toggling
    // nothing — which is why this can't simply stop on tr==Playing (see df5141a).
    const bool playingOurQueue = (tr == TransportState::Playing) && srcUri.startsWith("x-rincon-queue");
    if (s_st == St::Starting || playingOurQueue) stopPlayback();
    else                                         startPlaylist(1);
  } else if (ev == KnobEvent::Double) {
    startPlaylist(2);   // always starts — see the file header on why only x1 stops
  } else if (ev == KnobEvent::Triple) {
    startPlaylist(3);
  } else if (ev == KnobEvent::Long) {
    LOG.println("[unit   ] button Long — reserved");
  }

  // --- state machine ----------------------------------------------------------------------
  switch (s_st) {
    case St::Listing: {
      std::vector<String> labels;
      if (library::takeResults(labels)) {
        webConfigPlaylistsSet(labels);
        s_listed = true;
        s_st     = St::Idle;
        LOG.printf("[unit   ] %u playlists published to the config page\n",
                      (unsigned)labels.size());
        // Pre-warm the resolved-item cache so the very first press is already fast — every mapped
        // slot, since any of the three can be the first press after a reboot.
        for (uint8_t s = 1; s <= SETTINGS_PRESS_SLOTS; ++s)
          library::requestPlayNamed(settingsPlaylist(s), /*warmOnly=*/true);
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
        LOG.printf("[unit   ] playlist \"%s\" (press x%u) not found — set one on the config page\n",
                      settingsPlaylist(s_slot).c_str(), s_slot);
        s_st = St::Idle;
      } else if (tr == TransportState::Playing) {
        s_st = St::Playing;
        LOG.println("[unit   ] playing");
      } else if (now - s_startMs > START_TIMEOUT_MS) {
        LOG.println("[unit   ] timed out starting playback");
        s_st = St::Idle;
      }
      break;

    case St::Playing:
      // Stopped from the Sonos app, or the queue ran out despite REPEAT_ALL.
      if (tr == TransportState::Stopped || tr == TransportState::Paused) {
        s_st = St::Idle;
        LOG.println("[unit   ] stopped externally");
      }
      break;

    case St::Idle:
    default:
      break;
  }
}
