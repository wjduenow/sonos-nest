// sleep_machine unit — the Nursery ocean-sounds controller (ES3C28P, landscape 320x240).
//
// A single-purpose bedside appliance: touch control + status for the "Sleep" Sonos playlist
// (15x "Ocean Waves" white-noise track) on the Nursery speaker. It locks the shared core to
// the Nursery zone and drives playback entirely through the mutex-guarded globals + the async
// library:: service (netTask runs all SOAP; the UI only posts intent).
//
// Home (nothing playing): three options — Play from Cloud / Play from Local / Play on Device.
//   - Play from Cloud: set Nursery volume to 45, then enqueue+play the "Sleep" saved playlist
//     (library PLAY_PLAYLIST clears the queue, enqueues, and plays). 2 & 3 are placeholders.
// Playing: live status + a Stop control. See CLAUDE.md for the core/unit architecture.
#include "core/unit.h"
#include "core/player_state.h"
#include "core/library.h"
#include "core/settings.h"
#include "core/board.h"        // localAudioPlay/Stop/Active (option 3)
#include "ui_scale.h"
#include <lvgl.h>
#include <Arduino.h>
#include <vector>

// This appliance is bound to one room + one Sonos saved playlist.
static const char *TARGET_ROOM    = "Nursery";
static const char *SLEEP_PLAYLIST = "Sleep";   // exact saved-playlist title (SQ:0)
static const uint8_t SLEEP_VOLUME = 45;
// The same ocean track copied onto the microSD (played locally for "Play on Device").
static const char *LOCAL_OCEAN_FILE =
    "/Ocean Waves Crashing - Relaxing Sounds - Calming Relaxation Music For Sleeping - 1 Hour.mp3";

// Palette (deep-night theme, easy on the eyes in a dark nursery).
static const uint32_t COL_BG     = 0x0A1428;   // near-black navy
static const uint32_t COL_CLOUD  = 0x1E7A99;   // teal — primary action
static const uint32_t COL_SLATE  = 0x27324A;   // muted slate — secondary actions
static const uint32_t COL_STOP   = 0x8C2B2B;   // muted red
static const uint32_t COL_SUBTLE = 0x7C8AA5;   // grey-blue secondary text

enum UiState { ST_HOME, ST_STARTING, ST_PLAYING };
static UiState  s_state        = ST_HOME;
static uint32_t s_startMs       = 0;      // when the cloud sequence began (for timeout)
static bool     s_playRequested = false;  // have we posted requestPlay for this attempt yet
static bool     s_localMode     = false;  // ST_PLAYING via the onboard speaker, not Sonos

static lv_obj_t *s_home, *s_starting, *s_playing;
static lv_obj_t *s_playTitle, *s_volSlider;
static lv_obj_t *s_toast;
static uint32_t  s_toastUntil   = 0;
static uint32_t  s_volTouchedMs = 0;   // last time the user moved the volume slider
static uint8_t   s_localVol     = 60;  // remembered on-device (codec) volume, 0..100

// --- helpers ---------------------------------------------------------------

static void showOnly(lv_obj_t *keep) {
  lv_obj_t *pages[3] = {s_home, s_starting, s_playing};
  for (lv_obj_t *p : pages) {
    if (p == keep) lv_obj_remove_flag(p, LV_OBJ_FLAG_HIDDEN);
    else           lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
  }
}

static void showToast(const char *msg) {
  lv_label_set_text(s_toast, msg);
  lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
  s_toastUntil = millis() + 1800;
}

// The Sleep track's title is long ("Ocean Waves (One Hour Ocean Sounds ...)"); trim at the
// parenthetical for a clean heading. Falls back to a friendly default when nothing's set.
static String prettyTitle(const String &t) {
  int i = t.indexOf(" (");
  if (i > 0) return t.substring(0, i);
  return t.length() ? t : String("Ocean Waves");
}

static void gotoHome() {
  s_state = ST_HOME;
  s_playRequested = false;
  s_localMode = false;
  showOnly(s_home);
}

static void gotoStarting() {
  s_state = ST_STARTING;
  s_startMs = millis();
  s_playRequested = false;
  showOnly(s_starting);
}

static void gotoPlaying(const String &title, uint8_t vol) {
  s_localMode = false;
  lv_label_set_text(s_playTitle, prettyTitle(title).c_str());
  lv_slider_set_value(s_volSlider, vol, LV_ANIM_OFF);
  s_state = ST_PLAYING;
  showOnly(s_playing);
}

// Local playback (option 3): ocean track off the SD card through the onboard speaker.
static void gotoLocalPlaying() {
  s_localMode = true;
  lv_label_set_text(s_playTitle, "Ocean Waves");
  lv_slider_set_value(s_volSlider, s_localVol, LV_ANIM_OFF);
  s_state = ST_PLAYING;
  showOnly(s_playing);
}

// --- button callbacks ------------------------------------------------------

static void cloudCb(lv_event_t *) {
  // 1) volume -> 45 on the Nursery speaker (netTask applies it).
  if (stateLock()) { g_pending.targetVolume = SLEEP_VOLUME; stateUnlock(); }
  // 2) browse the saved playlists; uiTick picks "Sleep" from the results and plays it, which
  //    (PLAY_PLAYLIST) clears the queue, enqueues, and starts playback.
  library::requestBrowse("SQ:", library::PLAY_PLAYLIST);
  gotoStarting();
}

// Play from Local: serve the ocean MP3 off the SD card over HTTP and play it on the Nursery
// Sonos (looped). netTask enqueues the URL + REPEAT_ALL; this is Sonos playback, so it lands
// on the cloud now-playing screen (Sonos volume slider).
static void localCb(lv_event_t *) {
  const char *url = localFileUrl(LOCAL_OCEAN_FILE);
  if (!url) { showToast("SD / network unavailable"); return; }
  if (stateLock()) {
    g_pending.targetVolume   = SLEEP_VOLUME;   // 45, bedtime
    g_pending.localStreamUrl = url;
    stateUnlock();
  }
  gotoStarting();
}

// Play on Device: stream the ocean MP3 from the SD card through the onboard speaker. The first
// tap lazily mounts the SD + brings up the codec, so it can block briefly.
static void deviceCb(lv_event_t *) {
  if (localAudioPlay(LOCAL_OCEAN_FILE)) gotoLocalPlaying();
  else                                  showToast("No SD card / audio unavailable");
}

static void stopCb(lv_event_t *) {
  if (s_localMode) { localAudioStop(); gotoHome(); }
  else if (stateLock()) { g_pending.setPlay = 0; stateUnlock(); }
}

// Bottom volume slider: drives the Sonos speaker volume (cloud) or the ES8311 codec volume
// (on-device). Coalesced for Sonos via g_pending.targetVolume.
static void volSliderCb(lv_event_t *) {
  int v = lv_slider_get_value(s_volSlider);
  s_volTouchedMs = millis();
  if (s_localMode) { s_localVol = (uint8_t)v; localAudioSetVolume((uint8_t)v); }
  else if (stateLock()) { g_pending.targetVolume = v; stateUnlock(); }
}

// --- widget builders -------------------------------------------------------

static lv_obj_t *makePage(lv_obj_t *scr) {
  lv_obj_t *p = lv_obj_create(scr);
  lv_obj_remove_style_all(p);
  lv_obj_set_size(p, SCREEN_W, SCREEN_H);
  lv_obj_center(p);
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(p, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(p, SH(4), 0);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  return p;
}

static lv_obj_t *makeButton(lv_obj_t *parent, const char *text, uint32_t color, lv_event_cb_t cb) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_size(b, SW(88), SH(21));
  lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
  lv_obj_set_style_radius(b, SH(6), 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = lv_label_create(b);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(l, lv_color_white(), 0);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  return b;
}

static lv_obj_t *makeLabel(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, text);
  return l;
}

// --- unit contract ---------------------------------------------------------

void uiInit() {
  // Lock the shared core to the Nursery zone. main.cpp calls uiInit() before appBoot(), so
  // the core's selectZone() (saved room -> SONOS_DEFAULT_ROOM -> first) picks Nursery.
  settingsSetRoom(TARGET_ROOM);
  library::setLoopMode(true);   // cloud playback (option 1) loops the "Sleep" playlist forever

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // HOME — three options.
  s_home = makePage(scr);
  lv_obj_set_style_pad_row(s_home, SH(5), 0);
  makeButton(s_home, "Play from Cloud", COL_CLOUD, cloudCb);
  makeButton(s_home, "Play from Local", COL_SLATE, localCb);
  makeButton(s_home, "Play on Device",  COL_SLATE, deviceCb);

  // STARTING — brief transitional state while the playlist is enqueued.
  s_starting = makePage(scr);
  makeLabel(s_starting, LV_SYMBOL_AUDIO, &lv_font_montserrat_48, COL_CLOUD);
  makeLabel(s_starting, "Starting Ocean Waves\xE2\x80\xA6", &lv_font_montserrat_24, lv_color_to_u32(lv_color_white()));

  // PLAYING — status + Stop, with a volume slider anchored at the bottom (manual layout, not
  // flex, so the slider can sit at the bottom edge).
  s_playing = lv_obj_create(scr);
  lv_obj_remove_style_all(s_playing);
  lv_obj_set_size(s_playing, SCREEN_W, SCREEN_H);
  lv_obj_center(s_playing);
  lv_obj_remove_flag(s_playing, LV_OBJ_FLAG_SCROLLABLE);

  s_playTitle = makeLabel(s_playing, "Ocean Waves", &lv_font_montserrat_28, lv_color_to_u32(lv_color_white()));
  lv_obj_align(s_playTitle, LV_ALIGN_TOP_MID, 0, SH(22));

  lv_obj_t *stop = makeButton(s_playing, LV_SYMBOL_STOP "  Stop", COL_STOP, stopCb);
  lv_obj_align(stop, LV_ALIGN_CENTER, 0, -SH(2));

  lv_obj_t *volIcon = makeLabel(s_playing, LV_SYMBOL_VOLUME_MAX, &lv_font_montserrat_20, COL_SUBTLE);
  lv_obj_align(volIcon, LV_ALIGN_BOTTOM_LEFT, SW(6), -SH(11));

  s_volSlider = lv_slider_create(s_playing);
  lv_slider_set_range(s_volSlider, 0, 100);
  lv_obj_set_width(s_volSlider, SW(72));
  lv_obj_align(s_volSlider, LV_ALIGN_BOTTOM_MID, SW(8), -SH(13));
  lv_obj_set_style_bg_color(s_volSlider, lv_color_hex(COL_SLATE), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_volSlider, lv_color_hex(COL_CLOUD), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_volSlider, lv_color_hex(COL_CLOUD), LV_PART_KNOB);
  lv_obj_add_event_cb(s_volSlider, volSliderCb, LV_EVENT_VALUE_CHANGED, nullptr);

  // TOAST — transient bottom message for the placeholder actions / errors.
  s_toast = lv_label_create(scr);
  lv_obj_set_style_text_font(s_toast, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s_toast, lv_color_white(), 0);
  lv_obj_set_style_bg_color(s_toast, lv_color_hex(COL_SLATE), 0);
  lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_toast, SH(3), 0);
  lv_obj_set_style_radius(s_toast, SH(4), 0);
  lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -SH(4));
  lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

  gotoHome();
}

void uiTick() {
  lv_timer_handler();

  // Snapshot shared state under the lock.
  TransportState tr = TransportState::Unknown;
  String title;
  uint8_t vol = 0;
  if (stateLock()) {
    tr = g_player.transport;
    title = g_player.title;
    vol = g_player.volume;
    g_player.dirty = false;
    stateUnlock();
  }

  uint32_t now = millis();
  if (s_toastUntil && now > s_toastUntil) {
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    s_toastUntil = 0;
  }

  switch (s_state) {
    case ST_HOME:
      // Reflect Sonos playback started elsewhere (e.g. the app) so status stays honest.
      // (Don't do this while a local track is playing — that path owns ST_PLAYING.)
      if (tr == TransportState::Playing) gotoPlaying(title, vol);
      break;

    case ST_STARTING: {
      if (!s_playRequested) {
        std::vector<String> labels;
        if (library::takeResults(labels)) {     // browse finished
          int idx = -1;
          for (int i = 0; i < (int)labels.size(); ++i) {
            if (labels[i] == SLEEP_PLAYLIST) { idx = i; break; }
          }
          if (idx >= 0) { library::requestPlay(idx); s_playRequested = true; }
          else          { showToast("\xE2\x80\x98Sleep\xE2\x80\x99 playlist not found"); gotoHome(); }
        }
      }
      if (tr == TransportState::Playing)        gotoPlaying(title, vol);
      else if (now - s_startMs > 20000)         { showToast("Couldn\xE2\x80\x99t start playback"); gotoHome(); }
      break;
    }

    case ST_PLAYING:
      if (s_localMode) {
        if (!localAudioActive()) gotoHome();   // local track ended or was stopped
      } else if (tr == TransportState::Stopped || tr == TransportState::Paused) {
        gotoHome();
      } else {
        // Keep the live Sonos title fresh, and sync the slider to the speaker's volume —
        // but not while the user is dragging it or just did (so we don't fight the drag).
        lv_label_set_text(s_playTitle, prettyTitle(title).c_str());
        bool dragging = lv_obj_has_state(s_volSlider, LV_STATE_PRESSED);
        if (!dragging && now - s_volTouchedMs > 1500 &&
            lv_slider_get_value(s_volSlider) != vol) {
          lv_slider_set_value(s_volSlider, vol, LV_ANIM_OFF);
        }
      }
      break;
  }
}
