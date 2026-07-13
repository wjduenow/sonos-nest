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
#include "core/board.h"        // localAudioPlay/Stop/Active, backlightSet
#include "core/sonos/ssdp.h"   // sonos::zones() for the device picker
#include "core/net/wifi.h"     // wifiApply/result/ssid for Wi-Fi setup
#include "ui_scale.h"
#include <lvgl.h>
#include <Arduino.h>
#include <WiFi.h>              // scanNetworks for the Wi-Fi picker
#include <time.h>             // clock screensaver
#include <vector>

#ifndef DEVICE_HOSTNAME
#define DEVICE_HOSTNAME "sonos-sleep"
#endif

// This appliance is bound to one room + one Sonos saved playlist.
static const char *TARGET_ROOM    = "Nursery";
static const char *SLEEP_PLAYLIST = "Sleep";   // exact saved-playlist title (SQ:0)
static const uint8_t SLEEP_VOLUME = 45;
// Screensaver: after this much idle, dim the backlight and show a drifting clock (protects
// the LCD backlight over the many static hours a day; any tap wakes it back to the menu).
static const uint32_t SS_IDLE_MS = 60000;
static const uint8_t  SS_DIM     = 10;   // backlight % while the screensaver is showing
// Default local track on the microSD (options 2 & 3), used until one is picked in Settings.
static const char *LOCAL_OCEAN_FILE = "/Ocean.mp3";

// Palette (deep-night theme, easy on the eyes in a dark nursery).
static const uint32_t COL_BG     = 0x0A1428;   // near-black navy
static const uint32_t COL_CLOUD  = 0x1E7A99;   // teal   — Play from Cloud
static const uint32_t COL_LOCAL  = 0x2F7D57;   // green  — Play from Local
static const uint32_t COL_DEVICE = 0x6A4C93;   // purple — Play on Device
static const uint32_t COL_SLATE  = 0x27324A;   // muted slate — secondary actions
static const uint32_t COL_STOP   = 0x8C2B2B;   // muted red
static const uint32_t COL_WAKE   = 0xC2841F;   // warm amber — sunrise, the opposite of sleep
static const uint32_t COL_SUBTLE = 0x7C8AA5;   // grey-blue secondary text

// Home carousel: one large button per screen, cycled by swiping left/right (wraps both ways).
static const char    *HOME_LABELS[3] = {"Start Sleep Sounds",
                                        "Start From Local (No Internet)",
                                        "Start on Device (No Wifi)"};
static const uint32_t HOME_COLORS[3] = {COL_CLOUD, COL_LOCAL, COL_DEVICE};
static const char    *HOME_ICONS[3]  = {LV_SYMBOL_AUDIO, LV_SYMBOL_SD_CARD, LV_SYMBOL_VOLUME_MAX};
// Accent wash (top of the nightfall gradient) — a bold ~60% blend of each mode colour toward
// the navy ground, so the chosen mode floods the top of the screen with its colour.
static const uint32_t HOME_DEEP[3]   = {0x16516C, 0x205344, 0x443668};

enum UiState { ST_HOME, ST_STARTING, ST_PLAYING };
static UiState  s_state        = ST_HOME;
static uint32_t s_startMs       = 0;      // when the cloud sequence began (for timeout)
static bool     s_playRequested = false;  // have we posted requestPlay for this attempt yet
static bool     s_localMode     = false;  // ST_PLAYING via the onboard speaker, not Sonos
static String   s_sonosTitle;             // known title for a local-stream-on-Sonos ("" = use live)

static lv_obj_t *s_home, *s_starting, *s_playing, *s_settings, *s_rooms, *s_wifi, *s_wifiPw, *s_tracks, *s_nameEdit, *s_manager;
static lv_obj_t *s_homeBtn, *s_homeBtnLabel, *s_homeIcon, *s_dot[3];
static lv_obj_t *s_ghostL, *s_ghostR, *s_ghostLIcon, *s_ghostRIcon;
static lv_obj_t *s_trace[4];   // border comet: [0] = glowing head, [1..3] = fading tail
static int       s_homeIdx = 0;   // which carousel option (0..2) is showing
static lv_obj_t *s_playTitle, *s_volSlider, *s_startingLabel, *s_wakeBtn;
static lv_obj_t *s_briSlider, *s_sonosLabel, *s_wifiLabel, *s_trackLabel, *s_wakeLabel, *s_nameLabel;
static lv_obj_t *s_settingsList, *s_roomsList, *s_tracksList, *s_tracksHdr, *s_managerUrl;
static lv_obj_t *s_wifiList, *s_wifiSsidLabel, *s_wifiPwArea, *s_kb, *s_nameArea, *s_nameKb;
static lv_obj_t *s_toast;
static lv_obj_t *s_screensaver, *s_ssBox, *s_clockLabel, *s_timerLabel;
static lv_obj_t *s_curPage   = nullptr;   // page currently shown (screensaver return target)
static lv_obj_t *s_ssReturn  = nullptr;   // page to restore on wake
static bool      s_ssOn      = false;     // screensaver currently showing
static bool      s_ssShowTimer = false;   // screensaver includes the sleep-elapsed timer
static uint32_t  s_ssDriftMs = 0, s_ssClockMs = 0;
static uint8_t   s_ssDriftIdx = 0;
static uint32_t  s_playStartMs = 0;       // when the current playback started (for the timer)
static uint32_t  s_toastUntil   = 0;
static uint32_t  s_volTouchedMs = 0;   // last time the user moved the volume slider
static uint8_t   s_localVol     = 60;  // remembered on-device (codec) volume, 0..100
static String    s_localPath;          // file actually playing on the device speaker (Wake swaps it)
static bool      s_pickWake     = false;   // the track picker is choosing the wake track, not sleep
static bool      s_wakePlaying  = false;   // the wake track is what's playing — hide the Wake button
static std::vector<String> s_roomIps, s_roomNames;   // parallel to the device-picker rows
static std::vector<String> s_ssids;                  // scanned Wi-Fi networks
static String    s_pickedSsid;
static bool      s_wifiScanPending = false;
static bool      s_wifiConnecting  = false;

// --- helpers ---------------------------------------------------------------

static void showOnly(lv_obj_t *keep) {
  s_curPage = keep;
  lv_obj_t *pages[11] = {s_home, s_starting, s_playing, s_settings, s_rooms,
                         s_wifi, s_wifiPw, s_tracks, s_nameEdit, s_manager, s_screensaver};
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

// The track options 2 & 3 play: the user's pick (Settings -> Sleep Track), else the default.
static String currentTrack() {
  String t = settingsSleepTrack();
  return t.length() ? t : String(LOCAL_OCEAN_FILE);
}

// Display name for a track path: basename without the ".mp3" (e.g. "Thunder Storm").
static String displayNameOf(const String &path) {
  String d = path.substring(path.lastIndexOf('/') + 1);
  if (d.endsWith(".mp3")) d.remove(d.length() - 4);
  return d;
}

static String trackDisplayName() { return displayNameOf(currentTrack()); }

// The wake track, if there is one: the Settings pick when it's still on the card, else any file
// named "wake" (case-insensitive) so a freshly dropped Wake.mp3 just works. "" => no wake track,
// and the Now Playing screen hides the Wake button entirely.
static String wakeTrackPath() {
  String want = settingsWakeTrack();
  int n = localTrackCount();               // (re)scans the card if something changed it
  if (want.length()) {
    for (int i = 0; i < n; ++i) {
      const char *p = localTrackPath(i);
      if (p && want == p) return want;     // still there
    }
  }
  for (int i = 0; i < n; ++i) {            // no pick (or it's been deleted): fall back to /Wake.mp3
    const char *nm = localTrackName(i);    // basename, ".mp3" already stripped
    const char *p  = localTrackPath(i);
    if (nm && p && String(nm).equalsIgnoreCase("wake")) return String(p);
  }
  return "";
}

// Show the Wake button only when there's a wake track to switch TO: it needs to exist, and it
// mustn't already be what's playing (swapping wake for wake does nothing).
static void updateWakeBtn() {
  if (!s_wakeBtn) return;
  bool show = wakeTrackPath().length() && !s_wakePlaying;
  if (show) lv_obj_remove_flag(s_wakeBtn, LV_OBJ_FLAG_HIDDEN);
  else      lv_obj_add_flag(s_wakeBtn, LV_OBJ_FLAG_HIDDEN);
}

static void updateHome();   // fwd: refresh the carousel button + dots for s_homeIdx

static void gotoHome() {
  s_state = ST_HOME;
  s_playRequested = false;
  s_localMode = false;
  s_wakePlaying = false;
  s_sonosTitle = "";
  s_homeIdx = 0;              // always return to the default option (Start Sleep Sounds)
  updateHome();
  showOnly(s_home);
}

// Title for the Sonos now-playing screen: the known local-stream track name (option 2) when
// set, else the live Sonos-reported title (option 1 / playback started elsewhere).
static String sonosDisplayTitle(const String &liveTitle) {
  return s_sonosTitle.length() ? s_sonosTitle : prettyTitle(liveTitle);
}

static void gotoStarting(const String &what) {
  s_state = ST_STARTING;
  s_startMs = millis();
  s_playRequested = false;
  lv_label_set_text(s_startingLabel, (String("Starting ") + what + "...").c_str());
  showOnly(s_starting);
}

static void gotoPlaying(const String &title, uint8_t vol) {
  if (s_state != ST_PLAYING) s_playStartMs = millis();   // stamp the start on transition in
  s_localMode = false;
  lv_label_set_text(s_playTitle, sonosDisplayTitle(title).c_str());
  lv_slider_set_value(s_volSlider, vol, LV_ANIM_OFF);
  updateWakeBtn();
  s_state = ST_PLAYING;
  showOnly(s_playing);
}

// Local playback (option 3): the chosen track off the SD card through the onboard speaker.
// Titled from s_localPath, not currentTrack(), so a Wake swap shows the wake track's name.
static void gotoLocalPlaying() {
  if (s_state != ST_PLAYING) s_playStartMs = millis();   // stamp the start on transition in
  s_localMode = true;
  lv_label_set_text(s_playTitle, displayNameOf(s_localPath).c_str());
  lv_slider_set_value(s_volSlider, s_localVol, LV_ANIM_OFF);
  updateWakeBtn();
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
  s_sonosTitle = "";                 // cloud shows the live Sonos title (the Sleep playlist)
  s_wakePlaying = false;
  gotoStarting("Ocean Waves");
}

// Play from Local: serve the ocean MP3 off the SD card over HTTP and play it on the Nursery
// Sonos (looped). netTask enqueues the URL + REPEAT_ALL; this is Sonos playback, so it lands
// on the cloud now-playing screen (Sonos volume slider).
static void localCb(lv_event_t *) {
  String track = currentTrack();
  const char *url = localFileUrl(track.c_str());
  if (!url) { showToast("SD / network unavailable"); return; }
  if (stateLock()) {
    g_pending.targetVolume     = SLEEP_VOLUME;   // 45, bedtime
    g_pending.localStreamUrl   = url;
    g_pending.localStreamTitle = trackDisplayName();
    stateUnlock();
  }
  s_sonosTitle = trackDisplayName();   // show the real track name (Sonos caches by URL)
  s_wakePlaying = (track == wakeTrackPath());   // sleep track == wake track? nothing to swap to
  gotoStarting(trackDisplayName());
}

// Play on Device: stream the ocean MP3 from the SD card through the onboard speaker. The first
// tap lazily mounts the SD + brings up the codec, so it can block briefly.
static void deviceCb(lv_event_t *) {
  String track = currentTrack();
  if (localAudioPlay(track.c_str())) {
    s_localPath   = track;
    s_wakePlaying = (track == wakeTrackPath());   // sleep track == wake track? nothing to swap to
    gotoLocalPlaying();
  } else {
    showToast("No SD card / audio unavailable");
  }
}

static void stopCb(lv_event_t *) {
  if (s_localMode) { localAudioStop(); gotoHome(); }
  else if (stateLock()) { g_pending.setPlay = 0; stateUnlock(); }
}

// Wake: swap whatever is playing for the wake track, on the output that's already in use, at
// the volume that's already set. Stays on Now Playing — it reads as a swap, not a restart.
// Loops like the sleep tracks do (on-device loops by default; Sonos gets REPEAT_ALL).
static void wakeCb(lv_event_t *) {
  String wake = wakeTrackPath();
  if (!wake.length()) { showToast("No wake track on the SD card"); return; }

  if (s_localMode) {                       // on-device speaker: just hand the codec the new file
    if (!localAudioPlay(wake.c_str())) { showToast("Playback failed"); return; }
    s_localPath = wake;
    lv_label_set_text(s_playTitle, displayNameOf(wake).c_str());
  } else {                                 // Sonos — whether it was a playlist or our own stream
    const char *url = localFileUrl(wake.c_str());
    if (!url) { showToast("SD / network unavailable"); return; }
    if (stateLock()) {
      g_pending.localStreamUrl   = url;    // netTask: clear queue, enqueue, REPEAT_ALL, play
      g_pending.localStreamTitle = displayNameOf(wake);
      stateUnlock();                       // targetVolume deliberately untouched — volume stays put
    }
    s_sonosTitle = displayNameOf(wake);
    lv_label_set_text(s_playTitle, s_sonosTitle.c_str());
  }
  s_wakePlaying = true;   // wake IS the track now — drop the button, leaving Stop centered
  updateWakeBtn();
}

// --- home carousel ---------------------------------------------------------

// The one big button dispatches to the current option's action.
static void homeBtnCb(lv_event_t *e) {
  if (s_homeIdx == 0)      cloudCb(e);
  else if (s_homeIdx == 1) localCb(e);
  else                     deviceCb(e);
}

// ---- concept 1: nightfall wash — the screen colour cross-fades with the mode ----
static lv_color_t s_washFrom, s_washTo;
static void washExec(void *obj, int32_t v) {              // v: 0..255 colour lerp
  lv_obj_set_style_bg_color((lv_obj_t *)obj, lv_color_mix(s_washTo, s_washFrom, (uint8_t)v), 0);
}
static void washTo(uint32_t deepHex) {
  lv_anim_delete(s_home, washExec);                       // cancel any in-flight fade
  s_washFrom = lv_obj_get_style_bg_color(s_home, LV_PART_MAIN);
  s_washTo   = lv_color_hex(deepHex);
  lv_anim_t a; lv_anim_init(&a);
  lv_anim_set_var(&a, s_home);
  lv_anim_set_exec_cb(&a, washExec);
  lv_anim_set_values(&a, 0, 255);
  lv_anim_set_duration(&a, 500);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

// ---- concept 3: living glow — the tile's coloured shadow breathes ----
static void glowExec(void *obj, int32_t v) {              // v: 0..255 breathe
  lv_obj_t *b = (lv_obj_t *)obj;
  lv_obj_set_style_shadow_width(b, 14 + 16 * v / 255, 0);
  lv_obj_set_style_shadow_opa(b, (lv_opa_t)(LV_OPA_40 + (LV_OPA_70 - LV_OPA_40) * v / 255), 0);
}

// ---- border trace: a glowing comet runs clockwise around the tile's rounded border ----
// Map t in [0,1) to a point on the tile's rounded-rectangle outline (from the top-left corner).
static void borderPoint(float t, lv_coord_t *ox, lv_coord_t *oy) {
  const float W = SW(74), H = SH(54), R = SH(8);
  const float cx = SCREEN_W / 2.0f, cy = SCREEN_H / 2.0f - SH(5);   // tile centre (align CENTER, -SH(5))
  const float hw = W / 2, hh = H / 2;
  const float Lh = W - 2 * R, Lv = H - 2 * R, La = 1.5707963f * R;  // edge + quarter-arc lengths
  float d = t * (2 * Lh + 2 * Lv + 4 * La), x, y, a;
  if      (d < Lh)         { x = cx - (hw - R) + d;              y = cy - hh; }                                    // top edge →
  else if ((d -= Lh) < La) { a = -1.5708f + d / La * 1.5708f;    x = cx + (hw - R) + R * cosf(a); y = cy - (hh - R) + R * sinf(a); } // TR arc
  else if ((d -= La) < Lv) { x = cx + hw;                        y = cy - (hh - R) + d; }                          // right edge ↓
  else if ((d -= Lv) < La) { a = d / La * 1.5708f;               x = cx + (hw - R) + R * cosf(a); y = cy + (hh - R) + R * sinf(a); } // BR arc
  else if ((d -= La) < Lh) { x = cx + (hw - R) - d;              y = cy + hh; }                                    // bottom edge ←
  else if ((d -= Lh) < La) { a = 1.5708f + d / La * 1.5708f;     x = cx - (hw - R) + R * cosf(a); y = cy + (hh - R) + R * sinf(a); } // BL arc
  else if ((d -= La) < Lv) { x = cx - hw;                        y = cy + (hh - R) - d; }                          // left edge ↑
  else       { d -= Lv;      a = 3.14159f + d / La * 1.5708f;    x = cx - (hw - R) + R * cosf(a); y = cy - (hh - R) + R * sinf(a); } // TL arc
  *ox = (lv_coord_t)(x + 0.5f); *oy = (lv_coord_t)(y + 0.5f);
}
static void traceExec(void *, int32_t v) {                // v: 0..1000 lap phase
  for (int i = 0; i < 4; ++i) {
    float t = v / 1000.0f - i * 0.02f;                    // each tail dot trails the head along the path
    t -= (int)t; if (t < 0) t += 1.0f;                    // wrap into [0,1)
    lv_coord_t x, y; borderPoint(t, &x, &y);
    lv_coord_t half = lv_obj_get_width(s_trace[i]) / 2;
    lv_obj_set_pos(s_trace[i], x - half, y - half);       // centre the dot on the outline point
  }
}
static void traceShow(bool on) {                          // hidden during a slide so it doesn't float
  for (int i = 0; i < 4; ++i) {
    if (on) lv_obj_remove_flag(s_trace[i], LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(s_trace[i], LV_OBJ_FLAG_HIDDEN);
  }
}
static void slideDone(lv_anim_t *) { traceShow(true); }

// ---- concept 4: depth carousel — a dim ghost of the neighbouring option peeks at each edge ----
static lv_obj_t *makeGhost(lv_align_t align, lv_coord_t xoff, lv_obj_t **iconOut) {
  lv_obj_t *g = lv_obj_create(s_home);
  lv_obj_remove_style_all(g);
  lv_obj_remove_flag(g, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_add_flag(g, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_size(g, SW(20), SH(44));
  lv_obj_align(g, align, xoff, -SH(5));
  lv_obj_set_style_radius(g, SH(7), 0);
  lv_obj_set_style_bg_color(g, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(g, LV_OPA_10, 0);
  lv_obj_set_style_border_width(g, 1, 0);
  lv_obj_set_style_border_color(g, lv_color_hex(COL_SUBTLE), 0);
  lv_obj_set_style_border_opa(g, LV_OPA_30, 0);
  lv_obj_set_style_opa(g, LV_OPA_40, 0);              // whole ghost is dim
  lv_obj_t *ic = lv_label_create(g);
  lv_obj_set_style_text_font(ic, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(ic, lv_color_hex(COL_SUBTLE), 0);
  lv_obj_center(ic);
  *iconOut = ic;
  return g;
}

static void updateHome() {
  lv_color_t accent = lv_color_hex(HOME_COLORS[s_homeIdx]);
  lv_color_t light  = lv_color_lighten(accent, 90);   // bright accent for edge / text / icon
  lv_label_set_text(s_homeIcon, HOME_ICONS[s_homeIdx]);
  lv_label_set_text(s_homeBtnLabel, HOME_LABELS[s_homeIdx]);
  lv_obj_set_style_bg_color(s_homeBtn, accent, 0);       // faint tinted glass (low opacity)
  lv_obj_set_style_border_color(s_homeBtn, light, 0);    // bright accent edge
  lv_obj_set_style_shadow_color(s_homeBtn, accent, 0);   // colored glow
  lv_obj_set_style_text_color(s_homeIcon, light, 0);
  lv_obj_set_style_text_color(s_homeBtnLabel, light, 0);
  for (int i = 0; i < 3; ++i)
    lv_obj_set_style_bg_color(s_dot[i], lv_color_hex(i == s_homeIdx ? COL_SUBTLE : COL_SLATE), 0);
  lv_label_set_text(s_ghostLIcon, HOME_ICONS[(s_homeIdx + 2) % 3]);   // prev option peeks left
  lv_label_set_text(s_ghostRIcon, HOME_ICONS[(s_homeIdx + 1) % 3]);   // next option peeks right
  for (int i = 0; i < 4; ++i)
    lv_obj_set_style_bg_color(s_trace[i], light, 0);      // border comet takes the mode accent
  lv_obj_set_style_shadow_color(s_trace[0], light, 0);    // its glowing head too
  washTo(HOME_DEEP[s_homeIdx]);                            // cross-fade the screen wash
}

static void homeSlideXCb(void *obj, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t *)obj, v, 0); }

// Slide the (already-updated) button in from `dir` (+1 = from the right, -1 = from the left).
static void homeSlide(int dir) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_homeBtn);
  lv_anim_set_exec_cb(&a, homeSlideXCb);
  lv_anim_set_values(&a, dir * SCREEN_W, 0);
  lv_anim_set_duration(&a, 220);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_completed_cb(&a, slideDone);   // re-show the border comet once the tile settles
  traceShow(false);                          // hide it while the tile is mid-slide
  lv_anim_start(&a);
}

static void homeNext() { s_homeIdx = (s_homeIdx + 1) % 3; updateHome(); homeSlide(+1); }  // swipe left
static void homePrev() { s_homeIdx = (s_homeIdx + 2) % 3; updateHome(); homeSlide(-1); }  // swipe right

// Bottom volume row: drives the Sonos speaker volume (cloud) or the ES8311 codec volume
// (on-device). Coalesced for Sonos via g_pending.targetVolume. The - / + buttons flanking the
// slider nudge it by VOL_STEP — the slider itself is a small target for a fine adjustment.
static const int VOL_STEP = 2;

static void applyVolume(int v) {
  if (v < 0)   v = 0;
  if (v > 100) v = 100;
  lv_slider_set_value(s_volSlider, v, LV_ANIM_OFF);   // keep the slider in step with the buttons
  s_volTouchedMs = millis();                          // hold off the 1 Hz poll from overwriting us
  if (s_localMode) { s_localVol = (uint8_t)v; localAudioSetVolume((uint8_t)v); }
  else if (stateLock()) { g_pending.targetVolume = v; stateUnlock(); }
}

static void volSliderCb(lv_event_t *) { applyVolume(lv_slider_get_value(s_volSlider)); }
static void volDownCb(lv_event_t *)   { applyVolume(lv_slider_get_value(s_volSlider) - VOL_STEP); }
static void volUpCb(lv_event_t *)     { applyVolume(lv_slider_get_value(s_volSlider) + VOL_STEP); }

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
  lv_obj_add_flag(p, LV_OBJ_FLAG_EVENT_BUBBLE);   // let swipe gestures bubble up to the screen
  return p;
}

static lv_obj_t *makeButton(lv_obj_t *parent, const char *text, uint32_t color, lv_event_cb_t cb) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_size(b, SW(88), SH(21));
  lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
  lv_obj_set_style_radius(b, SH(6), 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);   // let swipe gestures reach the page below
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = lv_label_create(b);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(l, lv_color_white(), 0);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  return b;
}

// Glass / outlined button (same look as the home carousel): faint accent-tinted fill, bright
// accent border, soft accent glow, accent text, and a pressed state (brighter + push down).
static lv_obj_t *makeGlassButton(lv_obj_t *parent, const char *text, uint32_t accentHex,
                                 lv_coord_t w, lv_coord_t h, lv_event_cb_t cb) {
  lv_color_t accent = lv_color_hex(accentHex);
  lv_color_t light  = lv_color_lighten(accent, 90);
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, SH(6), 0);
  lv_obj_set_style_bg_color(b, accent, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_30, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_border_color(b, light, 0);
  lv_obj_set_style_border_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(b, 14, 0);
  lv_obj_set_style_shadow_spread(b, 0, 0);
  lv_obj_set_style_shadow_color(b, accent, 0);
  lv_obj_set_style_shadow_opa(b, LV_OPA_50, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_50, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_opa(b, LV_OPA_80, LV_STATE_PRESSED);
  lv_obj_set_style_translate_y(b, SH(1), LV_STATE_PRESSED);
  lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = lv_label_create(b);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(l, light, 0);
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

// Compact full-width list row (font 20, ellipsized) — for long entries like track names.
static lv_obj_t *makeListButton(lv_obj_t *parent, const char *text, uint32_t color, lv_event_cb_t cb) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_size(b, SW(92), SH(16));
  lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
  lv_obj_set_style_radius(b, SH(4), 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = lv_label_create(b);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(l, lv_color_white(), 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_width(l, SW(84));
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  return b;
}

// --- settings + device picker ----------------------------------------------

static void openSettings();
static void openRooms();
static void openWifi();
static void openWifiPw(const String &ssid);
static void openTracks(bool wake);   // one picker, two purposes: the sleep track or the wake track
static void openNameEdit();
static void openManager();           // shows the file-management web UI's URL

// One gesture handler on the screen (gestures from pages/buttons bubble up to it): swipe down
// from Home opens Settings; swipe up from Settings closes it.
static void screenGestureCb(lv_event_t *) {
  lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
  if (!lv_obj_has_flag(s_home, LV_OBJ_FLAG_HIDDEN)) {
    if (dir == LV_DIR_BOTTOM)     openSettings();
    else if (dir == LV_DIR_LEFT)  homeNext();   // swipe left  -> next option (wraps)
    else if (dir == LV_DIR_RIGHT) homePrev();   // swipe right -> previous option (wraps)
  }
  // Settings scrolls, so it closes via its Back button (swipe-up would fight the scroll).
}

static void briSliderCb(lv_event_t *) {
  int v = lv_slider_get_value(s_briSlider);
  if (v < 10) v = 10;
  backlightSet((uint8_t)v);
  settingsSetBrightness((uint8_t)v);
}

static void sonosBtnCb(lv_event_t *)  { openRooms(); }
static void wifiBtnCb(lv_event_t *)   { openWifi(); }
static void trackBtnCb(lv_event_t *)  { openTracks(false); }
static void wakeBtnCb(lv_event_t *)   { openTracks(true); }
static void managerBtnCb(lv_event_t *){ openManager(); }
static void nameBtnCb(lv_event_t *)   { openNameEdit(); }
static void backSettingsCb(lv_event_t *) { openSettings(); }
static void backHomeCb(lv_event_t *)     { gotoHome(); }

static void roomClickCb(lv_event_t *e) {
  lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
  int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
  if (idx < 0 || idx >= (int)s_roomIps.size()) return;
  settingsSetRoom(s_roomNames[idx]);                                  // persist the choice
  if (stateLock()) { g_pending.requestZoneIp = s_roomIps[idx]; stateUnlock(); }
  openSettings();
}

static void openSettings() {
  lv_slider_set_value(s_briSlider, settingsBrightness(), LV_ANIM_OFF);
  String room;
  if (stateLock()) { room = g_player.zoneName; stateUnlock(); }
  if (room.length() == 0) room = settingsRoom();
  lv_label_set_text(s_sonosLabel,
                    (String(LV_SYMBOL_AUDIO "  ") + (room.length() ? room : String("Sonos Device"))).c_str());
  String ssid = wifiSsid();
  lv_label_set_text(s_wifiLabel,
                    (String(LV_SYMBOL_WIFI "  ") + (ssid.length() ? ssid : String("Wi-Fi Setup"))).c_str());
  String tr = currentTrack();
  int slash = tr.lastIndexOf('/');
  String disp = tr.substring(slash + 1);
  if (disp.endsWith(".mp3")) disp.remove(disp.length() - 4);
  lv_label_set_text(s_trackLabel, (String(LV_SYMBOL_AUDIO "  ") + disp).c_str());
  String wk = wakeTrackPath();   // "" when the card has none — say so rather than showing blank
  lv_label_set_text(s_wakeLabel,
                    (String(LV_SYMBOL_BELL "  ") + (wk.length() ? displayNameOf(wk) : String("None"))).c_str());
  String dn = settingsDeviceName();
  lv_label_set_text(s_nameLabel, (String(LV_SYMBOL_LIST "  ") + (dn.length() ? dn : String(DEVICE_HOSTNAME))).c_str());
  showOnly(s_settings);
}

static void openRooms() {
  lv_obj_clean(s_roomsList);
  s_roomIps.clear();
  s_roomNames.clear();
  String cur;
  if (stateLock()) { cur = g_player.zoneName; stateUnlock(); }
  const std::vector<sonos::Zone> &zs = sonos::zones();
  if (zs.empty()) makeLabel(s_roomsList, "Searching...", &lv_font_montserrat_20, COL_SUBTLE);
  for (size_t i = 0; i < zs.size(); ++i) {
    lv_obj_t *b = makeButton(s_roomsList, zs[i].name.c_str(),
                             zs[i].name == cur ? COL_CLOUD : COL_SLATE, roomClickCb);
    lv_obj_set_user_data(b, (void *)(intptr_t)i);
    s_roomIps.push_back(zs[i].ip);
    s_roomNames.push_back(zs[i].name);
  }
  showOnly(s_rooms);
}

// --- Wi-Fi setup (scan + on-screen keyboard) -------------------------------

static void ssidClickCb(lv_event_t *e) {
  lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
  int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
  if (idx >= 0 && idx < (int)s_ssids.size()) openWifiPw(s_ssids[idx]);
}

// Keyboard OK: post the new creds to netTask (which applies + persists), and show a
// "Connecting..." state that uiTick resolves via wifiApplyResult().
static void kbReadyCb(lv_event_t *) {
  String pw = lv_textarea_get_text(s_wifiPwArea);
  wifiApplyResultReset();
  if (stateLock()) { g_pending.wifiSsid = s_pickedSsid; g_pending.wifiPass = pw; stateUnlock(); }
  s_wifiConnecting = true;
  lv_obj_clean(s_wifiList);
  makeLabel(s_wifiList, (String("Connecting to ") + s_pickedSsid + "...").c_str(),
            &lv_font_montserrat_20, lv_color_to_u32(lv_color_white()));
  showOnly(s_wifi);
}
static void kbCancelCb(lv_event_t *) { openWifi(); }

static void openWifi() {
  s_wifiConnecting = false;
  lv_obj_clean(s_wifiList);
  s_ssids.clear();
  makeLabel(s_wifiList, "Scanning" LV_SYMBOL_REFRESH, &lv_font_montserrat_20, COL_SUBTLE);
  WiFi.scanDelete();
  WiFi.scanNetworks(true /* async */);
  s_wifiScanPending = true;
  showOnly(s_wifi);
}

static void openWifiPw(const String &ssid) {
  s_pickedSsid = ssid;
  lv_label_set_text(s_wifiSsidLabel, ssid.c_str());
  lv_textarea_set_text(s_wifiPwArea, "");
  lv_keyboard_set_textarea(s_kb, s_wifiPwArea);
  showOnly(s_wifiPw);
}

// --- track picker (MP3s on the SD card) ------------------------------------
// Shared by both Settings rows; s_pickWake says which one opened it.

static void trackClickCb(lv_event_t *e) {
  lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
  int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
  const char *p = localTrackPath(idx);
  if (p) {
    if (s_pickWake) { settingsSetWakeTrack(p);  showToast("Wake track set");  }
    else            { settingsSetSleepTrack(p); showToast("Sleep track set"); }
  }
  openSettings();
}

static void openTracks(bool wake) {
  s_pickWake = wake;
  localTracksRefresh();
  lv_obj_clean(s_tracksList);
  int n = localTrackCount();
  String cur = wake ? wakeTrackPath() : currentTrack();
  lv_label_set_text(s_tracksHdr, wake ? "Wake Track" : "Sleep Track");
  if (n == 0) {
    makeLabel(s_tracksList, "No MP3s on the SD card", &lv_font_montserrat_20, COL_SUBTLE);
  }
  for (int i = 0; i < n; ++i) {
    const char *name = localTrackName(i);
    const char *path = localTrackPath(i);
    bool sel = path && (cur == path);
    lv_obj_t *b = makeListButton(s_tracksList, name ? name : "?", sel ? COL_CLOUD : COL_SLATE, trackClickCb);
    lv_obj_set_user_data(b, (void *)(intptr_t)i);
  }
  showOnly(s_tracks);
}

// --- file manager URL ------------------------------------------------------
// Read-only: the address of the on-device web UI for adding/removing tracks. The board owns the
// port, so we just display whatever localManagerUrl() hands back (nullptr => no WiFi yet).

static void openManager() {
  const char *url = localManagerUrl();
  lv_label_set_text(s_managerUrl, url ? url : "Not connected to Wi-Fi");
  showOnly(s_manager);
}

// --- device name (network hostname) ----------------------------------------

static void openNameEdit() {
  String cur = settingsDeviceName();
  if (cur.length() == 0) cur = DEVICE_HOSTNAME;
  lv_textarea_set_text(s_nameArea, cur.c_str());
  lv_keyboard_set_textarea(s_nameKb, s_nameArea);
  showOnly(s_nameEdit);
}

// Keyboard OK: sanitize to a valid hostname (letters/digits/hyphen), save, and reconnect so
// the router registers the new name.
static void nameKbReadyCb(lv_event_t *) {
  String raw = lv_textarea_get_text(s_nameArea);
  String h;
  for (unsigned i = 0; i < raw.length(); ++i) {
    char c = raw[i];
    bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (alnum)                                  h += c;
    else if (c == ' ' || c == '_' || c == '-')  h += '-';
  }
  while (h.startsWith("-")) h.remove(0, 1);
  while (h.endsWith("-"))   h.remove(h.length() - 1);
  if (h.length()) {
    settingsSetDeviceName(h);
    if (stateLock()) { g_pending.reconnectWifi = true; stateUnlock(); }
    showToast("Name saved");
  }
  openSettings();
}

// --- clock screensaver -----------------------------------------------------

static void ssUpdateClock() {
  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);
  if (ti.tm_year > 118) {   // year >= 2018 => NTP time is set
    char buf[16];
    strftime(buf, sizeof(buf), "%I:%M %p", &ti);
    lv_label_set_text(s_clockLabel, buf[0] == '0' ? buf + 1 : buf);   // trim leading zero
  } else {
    lv_label_set_text(s_clockLabel, "--:--");
  }
}

// Running elapsed time since playback started, as a ticking timer.
static void ssUpdateTimer() {
  uint32_t sec = (millis() - s_playStartMs) / 1000;
  char buf[24];
  snprintf(buf, sizeof(buf), LV_SYMBOL_PLAY "  %u:%02u:%02u",
           (unsigned)(sec / 3600), (unsigned)((sec % 3600) / 60), (unsigned)(sec % 60));
  lv_label_set_text(s_timerLabel, buf);
}

// Nudge the whole box to a new position periodically so it never sits on the same pixels.
static void ssDrift() {
  static const int8_t off[8][2] = {{0,-14},{16,-7},{18,9},{5,16},{-12,15},{-18,3},{-10,-12},{9,-11}};
  s_ssDriftIdx = (s_ssDriftIdx + 1) & 7;
  lv_obj_align(s_ssBox, LV_ALIGN_CENTER, off[s_ssDriftIdx][0], off[s_ssDriftIdx][1]);
}

static void enterScreensaver() {
  s_ssReturn = s_curPage;
  s_ssOn = true;
  s_ssShowTimer = (s_ssReturn == s_playing);   // show the sleep timer only while playing
  s_ssDriftMs = s_ssClockMs = millis();
  if (s_ssShowTimer) { lv_obj_remove_flag(s_timerLabel, LV_OBJ_FLAG_HIDDEN); ssUpdateTimer(); }
  else               { lv_obj_add_flag(s_timerLabel, LV_OBJ_FLAG_HIDDEN); }
  ssUpdateClock();
  ssDrift();
  showOnly(s_screensaver);
  backlightSet(SS_DIM);
}

static void exitScreensaver() {
  s_ssOn = false;
  backlightSet(settingsBrightness());
  showOnly(s_ssReturn ? s_ssReturn : s_home);
}

// --- unit contract ---------------------------------------------------------

void uiInit() {
  // Default the shared core to the Nursery zone on first boot only, so a device picked in
  // Settings persists. main.cpp calls uiInit() before appBoot(), so selectZone() sees it.
  if (settingsRoom().length() == 0) settingsSetRoom(TARGET_ROOM);
  library::setLoopMode(true);   // cloud playback (option 1) loops the "Sleep" playlist forever

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // HOME — a one-button carousel; swipe left/right to cycle the three options.
  s_home = lv_obj_create(scr);
  lv_obj_remove_style_all(s_home);
  lv_obj_set_size(s_home, SCREEN_W, SCREEN_H);
  lv_obj_center(s_home);
  lv_obj_remove_flag(s_home, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_home, LV_OBJ_FLAG_EVENT_BUBBLE);   // let swipes reach the screen handler

  // Nightfall wash (concept 1): vertical gradient from the mode's deep accent at the top down
  // into the navy ground; washTo() cross-fades the top colour as the option changes.
  lv_obj_set_style_bg_opa(s_home, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_home, lv_color_hex(HOME_DEEP[0]), 0);
  lv_obj_set_style_bg_grad_color(s_home, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_grad_dir(s_home, LV_GRAD_DIR_VER, 0);
  s_washFrom = s_washTo = lv_color_hex(HOME_DEEP[0]);

  s_homeBtn = lv_button_create(s_home);
  lv_obj_remove_style_all(s_homeBtn);            // full control of the glass look
  lv_obj_set_size(s_homeBtn, SW(74), SH(54));    // narrowed so the depth ghosts peek at the edges
  lv_obj_align(s_homeBtn, LV_ALIGN_CENTER, 0, -SH(5));
  lv_obj_add_flag(s_homeBtn, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(s_homeBtn, homeBtnCb, LV_EVENT_CLICKED, nullptr);
  // Glass / outlined base: faint tinted fill, bright accent border, soft accent glow (colors
  // per option are applied in updateHome). Pressed = brighter fill/glow + a small push down.
  lv_obj_set_style_radius(s_homeBtn, SH(8), 0);
  lv_obj_set_style_bg_opa(s_homeBtn, LV_OPA_30, 0);
  lv_obj_set_style_border_width(s_homeBtn, 2, 0);
  lv_obj_set_style_border_opa(s_homeBtn, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(s_homeBtn, 18, 0);
  lv_obj_set_style_shadow_spread(s_homeBtn, 0, 0);
  lv_obj_set_style_shadow_opa(s_homeBtn, LV_OPA_50, 0);
  lv_obj_set_style_bg_opa(s_homeBtn, LV_OPA_50, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_opa(s_homeBtn, LV_OPA_80, LV_STATE_PRESSED);
  lv_obj_set_style_translate_y(s_homeBtn, SH(1), LV_STATE_PRESSED);
  // Content: icon over wrapping text, centered.
  lv_obj_set_flex_flow(s_homeBtn, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_homeBtn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(s_homeBtn, SH(2), 0);
  lv_obj_set_style_pad_row(s_homeBtn, SH(2), 0);
  s_homeIcon = lv_label_create(s_homeBtn);
  lv_obj_set_style_text_font(s_homeIcon, &lv_font_montserrat_28, 0);
  s_homeBtnLabel = lv_label_create(s_homeBtn);
  lv_obj_set_style_text_font(s_homeBtnLabel, &lv_font_montserrat_24, 0);
  lv_label_set_long_mode(s_homeBtnLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_homeBtnLabel, SW(64));
  lv_obj_set_style_text_align(s_homeBtnLabel, LV_TEXT_ALIGN_CENTER, 0);

  // Living glow (concept 3): the tile's coloured shadow breathes on a slow ping-pong loop.
  { lv_anim_t g; lv_anim_init(&g);
    lv_anim_set_var(&g, s_homeBtn);
    lv_anim_set_exec_cb(&g, glowExec);
    lv_anim_set_values(&g, 0, 255);
    lv_anim_set_duration(&g, 2000);
    lv_anim_set_reverse_duration(&g, 2000);
    lv_anim_set_repeat_count(&g, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&g, lv_anim_path_ease_in_out);
    lv_anim_start(&g); }

  // Depth carousel (concept 4): dim ghost tiles peeking at each edge, showing the neighbouring
  // options so the carousel reads as having somewhere to go. Icons set per mode in updateHome().
  s_ghostL = makeGhost(LV_ALIGN_LEFT_MID,  -SW(9), &s_ghostLIcon);
  s_ghostR = makeGhost(LV_ALIGN_RIGHT_MID,  SW(9), &s_ghostRIcon);

  // Border trace: a glowing comet (bright head + short fading tail) runs around the tile's
  // rounded border. Drawn on top of the tile; colour set per mode in updateHome().
  static const lv_coord_t TR_SZ[4]  = {SH(4), SH(2), SH(2), SH(2)};
  static const lv_opa_t   TR_OPA[4] = {LV_OPA_COVER, LV_OPA_70, (lv_opa_t)110, (lv_opa_t)55};
  for (int i = 0; i < 4; ++i) {
    lv_obj_t *d = lv_obj_create(s_home);
    s_trace[i] = d;
    lv_obj_remove_style_all(d);
    lv_obj_remove_flag(d, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_flag(d, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(d, TR_SZ[i], TR_SZ[i]);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, TR_OPA[i], 0);
    if (i == 0) {                                    // the head glows
      lv_obj_set_style_shadow_width(d, SH(5), 0);
      lv_obj_set_style_shadow_spread(d, SH(1), 0);
      lv_obj_set_style_shadow_opa(d, LV_OPA_80, 0);
    }
  }
  traceExec(nullptr, 0);                             // place them before the first render (no flash)
  { lv_anim_t a; lv_anim_init(&a);                   // one continuous ~6 s lap (half speed)
    lv_anim_set_var(&a, s_trace[0]);
    lv_anim_set_exec_cb(&a, traceExec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 6000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a); }

  // page-position dots
  lv_obj_t *dots = lv_obj_create(s_home);
  lv_obj_remove_style_all(dots);
  lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -SH(8));
  lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(dots, SW(2), 0);
  for (int i = 0; i < 3; ++i) {
    s_dot[i] = lv_obj_create(dots);
    lv_obj_remove_style_all(s_dot[i]);
    lv_obj_set_size(s_dot[i], SH(3), SH(3));
    lv_obj_set_style_radius(s_dot[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_dot[i], LV_OPA_COVER, 0);
  }
  updateHome();

  // STARTING — brief transitional state while playback is enqueued (label set per track).
  s_starting = makePage(scr);
  makeLabel(s_starting, LV_SYMBOL_AUDIO, &lv_font_montserrat_48, COL_CLOUD);
  s_startingLabel = makeLabel(s_starting, "Starting...", &lv_font_montserrat_24, lv_color_to_u32(lv_color_white()));
  lv_obj_set_width(s_startingLabel, SW(92));
  lv_label_set_long_mode(s_startingLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s_startingLabel, LV_TEXT_ALIGN_CENTER, 0);

  // PLAYING — track name, Stop, and volume slider evenly spaced top-to-bottom (flex column).
  s_playing = lv_obj_create(scr);
  lv_obj_remove_style_all(s_playing);
  lv_obj_set_size(s_playing, SCREEN_W, SCREEN_H);
  lv_obj_center(s_playing);
  lv_obj_remove_flag(s_playing, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(s_playing, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_playing, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(s_playing, SH(7), 0);

  s_playTitle = makeLabel(s_playing, "Ocean Waves", &lv_font_montserrat_28, lv_color_to_u32(lv_color_white()));
  lv_obj_set_width(s_playTitle, SW(94));
  lv_label_set_long_mode(s_playTitle, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(s_playTitle, LV_TEXT_ALIGN_CENTER, 0);

  // Action row: Stop, plus Wake when the card has a wake track. Flex skips hidden children, so
  // with no wake track this collapses to a centered Stop — exactly the old layout.
  lv_obj_t *actRow = lv_obj_create(s_playing);
  lv_obj_remove_style_all(actRow);
  lv_obj_set_size(actRow, SW(94), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(actRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(actRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(actRow, SW(3), 0);
  lv_obj_add_flag(actRow, LV_OBJ_FLAG_EVENT_BUBBLE);
  makeGlassButton(actRow, LV_SYMBOL_STOP "  Stop", COL_STOP, SW(45), SH(24), stopCb);
  s_wakeBtn = makeGlassButton(actRow, LV_SYMBOL_BELL "  Wake", COL_WAKE, SW(45), SH(24), wakeCb);
  lv_obj_add_flag(s_wakeBtn, LV_OBJ_FLAG_HIDDEN);   // updateWakeBtn() reveals it when one exists

  // Volume row: [-] slider [+] as one flex item so it spaces evenly with the others.
  lv_obj_t *volRow = lv_obj_create(s_playing);
  lv_obj_remove_style_all(volRow);
  lv_obj_set_size(volRow, SW(94), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(volRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(volRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(volRow, SW(3), 0);
  makeGlassButton(volRow, LV_SYMBOL_MINUS, COL_CLOUD, SW(13), SH(11), volDownCb);
  s_volSlider = lv_slider_create(volRow);
  lv_slider_set_range(s_volSlider, 0, 100);
  lv_obj_set_width(s_volSlider, SW(60));
  lv_obj_set_style_bg_color(s_volSlider, lv_color_hex(COL_SLATE), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_volSlider, lv_color_hex(COL_CLOUD), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_volSlider, lv_color_hex(COL_CLOUD), LV_PART_KNOB);
  lv_obj_add_event_cb(s_volSlider, volSliderCb, LV_EVENT_VALUE_CHANGED, nullptr);
  makeGlassButton(volRow, LV_SYMBOL_PLUS, COL_CLOUD, SW(13), SH(11), volUpCb);

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

  // SETTINGS — Back-button header + a scrollable list (grows as settings are added).
  s_settings = lv_obj_create(scr);
  lv_obj_remove_style_all(s_settings);
  lv_obj_set_size(s_settings, SCREEN_W, SCREEN_H);
  lv_obj_center(s_settings);
  lv_obj_remove_flag(s_settings, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *setBack = makeButton(s_settings, LV_SYMBOL_LEFT " Back", COL_SLATE, backHomeCb);
  lv_obj_set_size(setBack, SW(28), SH(15));
  lv_obj_set_style_text_font(lv_obj_get_child(setBack, 0), &lv_font_montserrat_20, 0);
  lv_obj_align(setBack, LV_ALIGN_TOP_LEFT, SW(3), SH(3));
  lv_obj_t *setHdr = makeLabel(s_settings, "Settings", &lv_font_montserrat_20, lv_color_to_u32(lv_color_white()));
  lv_obj_align(setHdr, LV_ALIGN_TOP_MID, 0, SH(6));

  s_settingsList = lv_obj_create(s_settings);
  lv_obj_remove_style_all(s_settingsList);
  lv_obj_set_size(s_settingsList, SCREEN_W, SH(78));
  lv_obj_align(s_settingsList, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(s_settingsList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_settingsList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_settingsList, SH(3), 0);
  lv_obj_set_style_pad_top(s_settingsList, SH(2), 0);
  lv_obj_set_style_pad_bottom(s_settingsList, SH(4), 0);
  lv_obj_set_scroll_dir(s_settingsList, LV_DIR_VER);

  makeLabel(s_settingsList, "Brightness", &lv_font_montserrat_14, COL_SUBTLE);
  s_briSlider = lv_slider_create(s_settingsList);
  lv_slider_set_range(s_briSlider, 10, 100);
  lv_obj_set_width(s_briSlider, SW(80));
  lv_obj_set_style_bg_color(s_briSlider, lv_color_hex(COL_SLATE), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_briSlider, lv_color_hex(COL_CLOUD), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_briSlider, lv_color_hex(COL_CLOUD), LV_PART_KNOB);
  lv_obj_add_event_cb(s_briSlider, briSliderCb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_t *sonosBtn = makeListButton(s_settingsList, LV_SYMBOL_AUDIO "  Sonos Device", COL_SLATE, sonosBtnCb);
  s_sonosLabel = lv_obj_get_child(sonosBtn, 0);
  lv_obj_t *trackBtn = makeListButton(s_settingsList, LV_SYMBOL_AUDIO "  Sleep Track", COL_SLATE, trackBtnCb);
  s_trackLabel = lv_obj_get_child(trackBtn, 0);
  lv_obj_t *wakeRowBtn = makeListButton(s_settingsList, LV_SYMBOL_BELL "  Wake Track", COL_SLATE, wakeBtnCb);
  s_wakeLabel = lv_obj_get_child(wakeRowBtn, 0);
  lv_obj_t *wifiBtn = makeListButton(s_settingsList, LV_SYMBOL_WIFI "  Wi-Fi Setup", COL_SLATE, wifiBtnCb);
  s_wifiLabel = lv_obj_get_child(wifiBtn, 0);
  lv_obj_t *nameBtn = makeListButton(s_settingsList, LV_SYMBOL_LIST "  Device Name", COL_SLATE, nameBtnCb);
  s_nameLabel = lv_obj_get_child(nameBtn, 0);
  makeListButton(s_settingsList, LV_SYMBOL_DRIVE "  File Manager", COL_SLATE, managerBtnCb);

  // ROOMS — Sonos device picker (scrollable list of discovered rooms).
  s_rooms = lv_obj_create(scr);
  lv_obj_remove_style_all(s_rooms);
  lv_obj_set_size(s_rooms, SCREEN_W, SCREEN_H);
  lv_obj_center(s_rooms);
  lv_obj_remove_flag(s_rooms, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *rback = makeButton(s_rooms, LV_SYMBOL_LEFT " Back", COL_SLATE, backSettingsCb);
  lv_obj_set_size(rback, SW(28), SH(15));
  lv_obj_set_style_text_font(lv_obj_get_child(rback, 0), &lv_font_montserrat_20, 0);
  lv_obj_align(rback, LV_ALIGN_TOP_LEFT, SW(3), SH(3));
  lv_obj_t *rhdr = makeLabel(s_rooms, "Sonos Device", &lv_font_montserrat_20, lv_color_to_u32(lv_color_white()));
  lv_obj_align(rhdr, LV_ALIGN_TOP_MID, 0, SH(6));
  s_roomsList = lv_obj_create(s_rooms);
  lv_obj_remove_style_all(s_roomsList);
  lv_obj_set_size(s_roomsList, SCREEN_W, SH(70));
  lv_obj_align(s_roomsList, LV_ALIGN_BOTTOM_MID, 0, -SH(2));
  lv_obj_set_flex_flow(s_roomsList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_roomsList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_roomsList, SH(2), 0);

  // WIFI — network scan list (same layout as the rooms picker).
  s_wifi = lv_obj_create(scr);
  lv_obj_remove_style_all(s_wifi);
  lv_obj_set_size(s_wifi, SCREEN_W, SCREEN_H);
  lv_obj_center(s_wifi);
  lv_obj_remove_flag(s_wifi, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *wback = makeButton(s_wifi, LV_SYMBOL_LEFT " Back", COL_SLATE, backSettingsCb);
  lv_obj_set_size(wback, SW(28), SH(15));
  lv_obj_set_style_text_font(lv_obj_get_child(wback, 0), &lv_font_montserrat_20, 0);
  lv_obj_align(wback, LV_ALIGN_TOP_LEFT, SW(3), SH(3));
  lv_obj_t *whdr = makeLabel(s_wifi, "Wi-Fi", &lv_font_montserrat_20, lv_color_to_u32(lv_color_white()));
  lv_obj_align(whdr, LV_ALIGN_TOP_MID, 0, SH(6));
  s_wifiList = lv_obj_create(s_wifi);
  lv_obj_remove_style_all(s_wifiList);
  lv_obj_set_size(s_wifiList, SCREEN_W, SH(70));
  lv_obj_align(s_wifiList, LV_ALIGN_BOTTOM_MID, 0, -SH(2));
  lv_obj_set_flex_flow(s_wifiList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_wifiList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_wifiList, SH(2), 0);

  // WIFI PASSWORD — SSID label + text field + on-screen keyboard.
  s_wifiPw = lv_obj_create(scr);
  lv_obj_remove_style_all(s_wifiPw);
  lv_obj_set_size(s_wifiPw, SCREEN_W, SCREEN_H);
  lv_obj_center(s_wifiPw);
  lv_obj_remove_flag(s_wifiPw, LV_OBJ_FLAG_SCROLLABLE);
  s_wifiSsidLabel = makeLabel(s_wifiPw, "", &lv_font_montserrat_20, lv_color_to_u32(lv_color_white()));
  lv_obj_align(s_wifiSsidLabel, LV_ALIGN_TOP_MID, 0, SH(1));
  s_wifiPwArea = lv_textarea_create(s_wifiPw);
  lv_textarea_set_one_line(s_wifiPwArea, true);
  lv_textarea_set_placeholder_text(s_wifiPwArea, "password");
  lv_obj_set_width(s_wifiPwArea, SW(92));
  lv_obj_align(s_wifiPwArea, LV_ALIGN_TOP_MID, 0, SH(15));
  s_kb = lv_keyboard_create(s_wifiPw);
  lv_obj_set_size(s_kb, SCREEN_W, SH(58));
  lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(s_kb, s_wifiPwArea);
  lv_obj_add_event_cb(s_kb, kbReadyCb, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(s_kb, kbCancelCb, LV_EVENT_CANCEL, nullptr);

  // DEVICE NAME — text field + on-screen keyboard (same pattern as the Wi-Fi password).
  s_nameEdit = lv_obj_create(scr);
  lv_obj_remove_style_all(s_nameEdit);
  lv_obj_set_size(s_nameEdit, SCREEN_W, SCREEN_H);
  lv_obj_center(s_nameEdit);
  lv_obj_remove_flag(s_nameEdit, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *nameHdr = makeLabel(s_nameEdit, "Device Name", &lv_font_montserrat_20, lv_color_to_u32(lv_color_white()));
  lv_obj_align(nameHdr, LV_ALIGN_TOP_MID, 0, SH(1));
  s_nameArea = lv_textarea_create(s_nameEdit);
  lv_textarea_set_one_line(s_nameArea, true);
  lv_textarea_set_placeholder_text(s_nameArea, "name");
  lv_obj_set_width(s_nameArea, SW(92));
  lv_obj_align(s_nameArea, LV_ALIGN_TOP_MID, 0, SH(15));
  s_nameKb = lv_keyboard_create(s_nameEdit);
  lv_obj_set_size(s_nameKb, SCREEN_W, SH(58));
  lv_obj_align(s_nameKb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(s_nameKb, s_nameArea);
  lv_obj_add_event_cb(s_nameKb, nameKbReadyCb, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(s_nameKb, backSettingsCb, LV_EVENT_CANCEL, nullptr);

  // FILE MANAGER — read-only screen showing the URL of the on-device web UI, so you can type it
  // into a browser to add/remove tracks without pulling the SD card.
  s_manager = lv_obj_create(scr);
  lv_obj_remove_style_all(s_manager);
  lv_obj_set_size(s_manager, SCREEN_W, SCREEN_H);
  lv_obj_center(s_manager);
  lv_obj_remove_flag(s_manager, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *mback = makeButton(s_manager, LV_SYMBOL_LEFT " Back", COL_SLATE, backSettingsCb);
  lv_obj_set_size(mback, SW(28), SH(15));
  lv_obj_set_style_text_font(lv_obj_get_child(mback, 0), &lv_font_montserrat_20, 0);
  lv_obj_align(mback, LV_ALIGN_TOP_LEFT, SW(3), SH(3));
  lv_obj_t *mhdr = makeLabel(s_manager, "File Manager", &lv_font_montserrat_20, lv_color_to_u32(lv_color_white()));
  lv_obj_align(mhdr, LV_ALIGN_TOP_MID, 0, SH(6));
  // The URL wraps rather than ellipsizing — it's useless if you can't read all of it.
  s_managerUrl = makeLabel(s_manager, "", &lv_font_montserrat_24, lv_color_to_u32(lv_color_white()));
  lv_obj_set_width(s_managerUrl, SW(92));
  lv_label_set_long_mode(s_managerUrl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s_managerUrl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_managerUrl, LV_ALIGN_CENTER, 0, -SH(2));
  lv_obj_t *mhint = makeLabel(s_manager, "Open in a browser to add or\nremove tracks on the SD card",
                              &lv_font_montserrat_14, COL_SUBTLE);
  lv_obj_set_style_text_align(mhint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(mhint, LV_ALIGN_BOTTOM_MID, 0, -SH(8));

  // TRACK PICKER — lists the SD card's MP3s (same layout as the rooms picker). Serves both the
  // Sleep Track and Wake Track settings rows; openTracks(wake) retitles the header.
  s_tracks = lv_obj_create(scr);
  lv_obj_remove_style_all(s_tracks);
  lv_obj_set_size(s_tracks, SCREEN_W, SCREEN_H);
  lv_obj_center(s_tracks);
  lv_obj_remove_flag(s_tracks, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *tback = makeButton(s_tracks, LV_SYMBOL_LEFT " Back", COL_SLATE, backSettingsCb);
  lv_obj_set_size(tback, SW(28), SH(15));
  lv_obj_set_style_text_font(lv_obj_get_child(tback, 0), &lv_font_montserrat_20, 0);
  lv_obj_align(tback, LV_ALIGN_TOP_LEFT, SW(3), SH(3));
  s_tracksHdr = makeLabel(s_tracks, "Sleep Track", &lv_font_montserrat_20, lv_color_to_u32(lv_color_white()));
  lv_obj_align(s_tracksHdr, LV_ALIGN_TOP_MID, 0, SH(6));
  s_tracksList = lv_obj_create(s_tracks);
  lv_obj_remove_style_all(s_tracksList);
  lv_obj_set_size(s_tracksList, SCREEN_W, SH(70));
  lv_obj_align(s_tracksList, LV_ALIGN_BOTTOM_MID, 0, -SH(2));
  lv_obj_set_flex_flow(s_tracksList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_tracksList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_tracksList, SH(2), 0);

  // SCREENSAVER — dim drifting clock shown after idle; a tap wakes back to the menu.
  s_screensaver = lv_obj_create(scr);
  lv_obj_remove_style_all(s_screensaver);
  lv_obj_set_size(s_screensaver, SCREEN_W, SCREEN_H);
  lv_obj_center(s_screensaver);
  lv_obj_remove_flag(s_screensaver, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_screensaver, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_screensaver, LV_OPA_COVER, 0);
  s_ssBox = lv_obj_create(s_screensaver);
  lv_obj_remove_style_all(s_ssBox);
  lv_obj_set_size(s_ssBox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_center(s_ssBox);
  lv_obj_set_flex_flow(s_ssBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_ssBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_ssBox, SH(2), 0);
  s_clockLabel = makeLabel(s_ssBox, "--:--", &lv_font_montserrat_48, COL_SUBTLE);   // current time
  s_timerLabel = makeLabel(s_ssBox, "", &lv_font_montserrat_28, COL_SUBTLE);        // sleep elapsed

  lv_obj_add_event_cb(scr, screenGestureCb, LV_EVENT_GESTURE, nullptr);

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

  // Screensaver: dim clock after idle on the resting screens (home / now-playing); any touch
  // wakes it. While showing, refresh the time and drift its position, and skip the rest.
  uint32_t idle = lv_display_get_inactive_time(NULL);
  if (s_ssOn) {
    if (idle < 800) {
      exitScreensaver();                          // woke on touch -> fall through to normal UI
    } else {
      if (now - s_ssClockMs > 1000) {
        s_ssClockMs = now;
        ssUpdateClock();
        if (s_ssShowTimer) ssUpdateTimer();
      }
      if (now - s_ssDriftMs > 30000) { s_ssDriftMs = now; ssDrift(); }
      return;
    }
  } else if (idle > SS_IDLE_MS && (s_curPage == s_home || s_curPage == s_playing)) {
    enterScreensaver();
    return;
  }

  // Wi-Fi async scan finished -> populate the network list.
  if (s_wifiScanPending) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      s_wifiScanPending = false;
      lv_obj_clean(s_wifiList);
      s_ssids.clear();
      if (n == 0) makeLabel(s_wifiList, "No networks found", &lv_font_montserrat_20, COL_SUBTLE);
      for (int i = 0; i < n && (int)s_ssids.size() < 24; ++i) {
        String ss = WiFi.SSID(i);
        if (ss.length() == 0) continue;
        bool dup = false;
        for (auto &e : s_ssids) if (e == ss) { dup = true; break; }
        if (dup) continue;
        lv_obj_t *b = makeButton(s_wifiList, ss.c_str(), COL_SLATE, ssidClickCb);
        lv_obj_set_user_data(b, (void *)(intptr_t)s_ssids.size());
        s_ssids.push_back(ss);
      }
      WiFi.scanDelete();
    }
  }

  // Wi-Fi connect result (netTask ran wifiApply).
  if (s_wifiConnecting) {
    int r = wifiApplyResult();
    if (r == WIFI_APPLY_OK)   { s_wifiConnecting = false; showToast("Wi-Fi connected"); openSettings(); }
    else if (r == WIFI_APPLY_FAIL) { s_wifiConnecting = false; showToast("Couldn't connect"); openWifi(); }
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
          else          { showToast("'Sleep' playlist not found"); gotoHome(); }
        }
      }
      if (tr == TransportState::Playing)        gotoPlaying(title, vol);
      else if (now - s_startMs > 20000)         { showToast("Couldn't start playback"); gotoHome(); }
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
        lv_label_set_text(s_playTitle, sonosDisplayTitle(title).c_str());
        bool dragging = lv_obj_has_state(s_volSlider, LV_STATE_PRESSED);
        if (!dragging && now - s_volTouchedMs > 1500 &&
            lv_slider_get_value(s_volSlider) != vol) {
          lv_slider_set_value(s_volSlider, vol, LV_ANIM_OFF);
        }
      }
      break;
  }
}
