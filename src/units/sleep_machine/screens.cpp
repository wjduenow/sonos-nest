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
#include "ui_scale.h"
#include <lvgl.h>
#include <Arduino.h>
#include <vector>

// This appliance is bound to one room + one Sonos saved playlist.
static const char *TARGET_ROOM    = "Nursery";
static const char *SLEEP_PLAYLIST = "Sleep";   // exact saved-playlist title (SQ:0)
static const uint8_t SLEEP_VOLUME = 45;

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

static lv_obj_t *s_home, *s_starting, *s_playing;
static lv_obj_t *s_playTitle, *s_playStatus;
static lv_obj_t *s_toast;
static uint32_t  s_toastUntil = 0;

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
  showOnly(s_home);
}

static void gotoStarting() {
  s_state = ST_STARTING;
  s_startMs = millis();
  s_playRequested = false;
  showOnly(s_starting);
}

static void gotoPlaying(const String &title, uint8_t vol) {
  lv_label_set_text(s_playTitle, prettyTitle(title).c_str());
  char buf[48];
  snprintf(buf, sizeof(buf), LV_SYMBOL_PLAY "  Playing   ·   Vol %u", (unsigned)vol);
  lv_label_set_text(s_playStatus, buf);
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

static void localCb(lv_event_t *)  { showToast("Play from Local — coming soon"); }
static void deviceCb(lv_event_t *) { showToast("Play on Device — coming soon"); }
static void stopCb(lv_event_t *)   { if (stateLock()) { g_pending.setPlay = 0; stateUnlock(); } }

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

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // HOME — room header + three options.
  s_home = makePage(scr);
  lv_obj_set_style_pad_row(s_home, SH(5), 0);
  makeLabel(s_home, LV_SYMBOL_AUDIO "  Nursery", &lv_font_montserrat_20, COL_SUBTLE);
  makeButton(s_home, "Play from Cloud", COL_CLOUD, cloudCb);
  makeButton(s_home, "Play from Local", COL_SLATE, localCb);
  makeButton(s_home, "Play on Device",  COL_SLATE, deviceCb);

  // STARTING — brief transitional state while the playlist is enqueued.
  s_starting = makePage(scr);
  makeLabel(s_starting, LV_SYMBOL_AUDIO, &lv_font_montserrat_48, COL_CLOUD);
  makeLabel(s_starting, "Starting Ocean Waves\xE2\x80\xA6", &lv_font_montserrat_24, lv_color_to_u32(lv_color_white()));

  // PLAYING — live status + Stop.
  s_playing   = makePage(scr);
  s_playTitle = makeLabel(s_playing, "Ocean Waves", &lv_font_montserrat_28, lv_color_to_u32(lv_color_white()));
  makeLabel(s_playing, "Nursery", &lv_font_montserrat_20, COL_SUBTLE);
  s_playStatus = makeLabel(s_playing, LV_SYMBOL_PLAY "  Playing", &lv_font_montserrat_20, COL_SUBTLE);
  makeButton(s_playing, LV_SYMBOL_STOP "  Stop", COL_STOP, stopCb);

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
      // Reflect playback started elsewhere (e.g. the Sonos app) so status stays honest.
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
      if (tr == TransportState::Stopped || tr == TransportState::Paused) gotoHome();
      else gotoPlaying(title, vol);   // keep the live title/volume fresh
      break;
  }
}
