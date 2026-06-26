#include "screens.h"
#include "ui_scale.h"
#include "album_art.h"
#include "../player_state.h"
#include "../sonos/ssdp.h"
#include "../hw/encoder.h"
#include "../hw/pcf8574.h"
#include "../hw/display.h"
#include <lvgl.h>
#include <vector>
#include <time.h>

// Screens (plan §6):
//   NOW PLAYING — art, title/artist, progress arc, play-state, volume; prev/next buttons.
//     twist=volume  short press=play/pause  long press=open ROOMS  touch ◀/▶=prev/next
//   ROOMS — scrollable zone list; twist=scroll  short press=select  long press=cancel
//     (touch a row to pick it directly).
// TODO Phase 3: PLAYLISTS / RADIO browse lists (reuse this list pattern). Phase 4: CLOCK.

static Screen s_cur = Screen::NowPlaying;

// --- NOW PLAYING widgets ---
static lv_obj_t *s_scrNow;
static lv_obj_t *s_arc, *s_art, *s_title, *s_artist, *s_time, *s_zone, *s_vol;
static uint32_t  s_volShownAt;

// --- ROOMS widgets ---
static lv_obj_t *s_scrRooms;
static lv_obj_t *s_roomList;
static std::vector<String> s_roomIps;   // parallel to the list rows
static int s_roomSel = 0;

// --- CLOCK screensaver ---
static lv_obj_t *s_scrClock, *s_clkTime, *s_clkDate;
static Screen    s_prevScreen = Screen::NowPlaying;  // where to return on wake
static uint32_t  s_lastEncBtn = 0;                   // last encoder/button activity
static const uint32_t kSaverMs        = 30000;       // idle -> screensaver
static const uint8_t  kSaverBright    = 12;          // dimmed backlight %
static const uint8_t  kFullBright     = 100;

static void fmtTime(char *buf, size_t n, uint32_t sec) {
  snprintf(buf, n, "%lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
}

// ---------------- NOW PLAYING ----------------

static void prevCb(lv_event_t *) { if (stateLock()) { g_pending.prev = true; stateUnlock(); } }
static void nextCb(lv_event_t *) { if (stateLock()) { g_pending.next = true; stateUnlock(); } }

static lv_obj_t *makeNavBtn(lv_obj_t *scr, const char *sym, lv_align_t align, lv_event_cb_t cb) {
  lv_obj_t *b = lv_button_create(scr);
  lv_obj_set_size(b, SW(15), SW(15));
  lv_obj_align(b, align, align == LV_ALIGN_LEFT_MID ? SW(2) : -SW(2), 0);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_30, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, sym);
  lv_obj_center(l);
  return b;
}

static void buildNowPlaying() {
  s_scrNow = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_scrNow, lv_color_black(), 0);
  lv_obj_remove_flag(s_scrNow, LV_OBJ_FLAG_SCROLLABLE);

  s_arc = lv_arc_create(s_scrNow);
  lv_obj_set_size(s_arc, SW(98), SH(98));
  lv_obj_center(s_arc);
  lv_arc_set_rotation(s_arc, 135);
  lv_arc_set_bg_angles(s_arc, 0, 270);
  lv_arc_set_range(s_arc, 0, 1000);
  lv_arc_set_value(s_arc, 0);
  lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_style(s_arc, nullptr, LV_PART_KNOB);
  lv_obj_set_style_arc_width(s_arc, SW(2), LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_arc, SW(2), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_arc, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);

  s_art = lv_image_create(s_scrNow);
  lv_obj_align(s_art, LV_ALIGN_CENTER, 0, -SH(9));
  lv_obj_set_style_radius(s_art, SW(4), 0);
  lv_obj_set_style_clip_corner(s_art, true, 0);
  lv_obj_add_flag(s_art, LV_OBJ_FLAG_HIDDEN);

  s_zone = lv_label_create(s_scrNow);
  lv_obj_set_style_text_color(s_zone, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_text_font(s_zone, &lv_font_montserrat_14, 0);
  lv_label_set_text(s_zone, "");
  lv_obj_align(s_zone, LV_ALIGN_CENTER, 0, -SH(40));

  s_title = lv_label_create(s_scrNow);
  lv_obj_set_style_text_color(s_title, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(s_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(s_title, SW(70));
  lv_label_set_text(s_title, "—");
  lv_obj_align(s_title, LV_ALIGN_CENTER, 0, SH(14));

  s_artist = lv_label_create(s_scrNow);
  lv_obj_set_style_text_color(s_artist, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_text_font(s_artist, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_align(s_artist, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(s_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(s_artist, SW(70));
  lv_label_set_text(s_artist, "");
  lv_obj_align(s_artist, LV_ALIGN_CENTER, 0, SH(25));

  s_time = lv_label_create(s_scrNow);
  lv_obj_set_style_text_color(s_time, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_text_font(s_time, &lv_font_montserrat_14, 0);
  lv_label_set_text(s_time, LV_SYMBOL_STOP " 0:00 / 0:00");
  lv_obj_align(s_time, LV_ALIGN_CENTER, 0, SH(34));

  s_vol = lv_label_create(s_scrNow);
  lv_obj_set_style_text_color(s_vol, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_vol, &lv_font_montserrat_28, 0);
  lv_obj_set_style_bg_color(s_vol, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_vol, LV_OPA_70, 0);
  lv_obj_set_style_pad_all(s_vol, SW(2), 0);
  lv_obj_set_style_radius(s_vol, SW(3), 0);
  lv_obj_align(s_vol, LV_ALIGN_CENTER, 0, -SH(9));
  lv_obj_add_flag(s_vol, LV_OBJ_FLAG_HIDDEN);

  makeNavBtn(s_scrNow, LV_SYMBOL_PREV, LV_ALIGN_LEFT_MID, prevCb);
  makeNavBtn(s_scrNow, LV_SYMBOL_NEXT, LV_ALIGN_RIGHT_MID, nextCb);
}

static void renderNow() {
  TransportState st;
  uint32_t pos, dur;
  char title[128], artist[128], zone[40];
  if (!stateLock()) return;
  st  = g_player.transport;
  pos = g_player.positionSec;
  dur = g_player.durationSec;
  snprintf(title,  sizeof(title),  "%s", g_player.title.length() ? g_player.title.c_str() : "—");
  snprintf(artist, sizeof(artist), "%s", g_player.artist.c_str());
  snprintf(zone,   sizeof(zone),   "%s", g_player.zoneName.c_str());
  stateUnlock();

  lv_label_set_text(s_title, title);
  lv_label_set_text(s_artist, artist);
  lv_label_set_text(s_zone, zone);

  const char *sym = st == TransportState::Playing ? LV_SYMBOL_PLAY :
                    st == TransportState::Paused  ? LV_SYMBOL_PAUSE : LV_SYMBOL_STOP;
  char tbuf[40], a[8], b[8];
  fmtTime(a, sizeof(a), pos);
  fmtTime(b, sizeof(b), dur);
  snprintf(tbuf, sizeof(tbuf), "%s %s / %s", sym, a, b);
  lv_label_set_text(s_time, tbuf);
  lv_arc_set_value(s_arc, dur ? (int32_t)((uint64_t)pos * 1000 / dur) : 0);

  if (!lv_obj_has_flag(s_vol, LV_OBJ_FLAG_HIDDEN) && lv_tick_elaps(s_volShownAt) > 1500)
    lv_obj_add_flag(s_vol, LV_OBJ_FLAG_HIDDEN);
}

// ---------------- ROOMS ----------------

static void highlightRoom() {
  uint32_t n = lv_obj_get_child_count(s_roomList);
  for (uint32_t i = 0; i < n; ++i) {
    lv_obj_t *b = lv_obj_get_child(s_roomList, i);
    bool sel = ((int)i == s_roomSel);
    // Selected: solid blue fill. Unselected: transparent (black). Text stays white either
    // way so every row is legible.
    lv_obj_set_style_bg_opa(b, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(b, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_text_color(b, lv_color_white(), 0);
    lv_obj_set_style_text_font(b, &lv_font_montserrat_20, 0);  // ~1.5x the default 14
    if (sel) lv_obj_scroll_to_view(b, LV_ANIM_ON);
  }
}

static void selectRoom(int idx) {
  if (idx < 0 || idx >= (int)s_roomIps.size()) return;
  if (stateLock()) { g_pending.requestZoneIp = s_roomIps[idx]; stateUnlock(); }
  uiShow(Screen::NowPlaying);
}

static void roomClickCb(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  selectRoom(idx);
}

static void populateRooms() {
  lv_obj_clean(s_roomList);
  s_roomIps.clear();

  String cur;
  if (stateLock()) { cur = g_player.zoneName; stateUnlock(); }

  const std::vector<sonos::Zone> &zs = sonos::zones();
  int curIdx = 0;
  for (size_t i = 0; i < zs.size(); ++i) {
    lv_obj_t *btn = lv_list_add_button(s_roomList, LV_SYMBOL_AUDIO, zs[i].name.c_str());
    lv_obj_add_event_cb(btn, roomClickCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    s_roomIps.push_back(zs[i].ip);
    if (zs[i].name == cur) curIdx = (int)i;
  }
  s_roomSel = curIdx;
  highlightRoom();
}

static void buildRooms() {
  s_scrRooms = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_scrRooms, lv_color_black(), 0);
  lv_obj_remove_flag(s_scrRooms, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *hdr = lv_label_create(s_scrRooms);
  lv_obj_set_style_text_color(hdr, lv_color_white(), 0);
  lv_obj_set_style_text_font(hdr, &lv_font_montserrat_20, 0);
  lv_label_set_text(hdr, "Rooms");
  lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, SH(12));

  s_roomList = lv_list_create(s_scrRooms);
  lv_obj_set_size(s_roomList, SW(80), SH(64));
  lv_obj_align(s_roomList, LV_ALIGN_CENTER, 0, SH(6));
  lv_obj_set_style_bg_opa(s_roomList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_roomList, 0, 0);
}

// ---------------- CLOCK screensaver ----------------

static void buildClock() {
  s_scrClock = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_scrClock, lv_color_black(), 0);
  lv_obj_remove_flag(s_scrClock, LV_OBJ_FLAG_SCROLLABLE);

  s_clkTime = lv_label_create(s_scrClock);
  lv_obj_set_style_text_color(s_clkTime, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_clkTime, &lv_font_montserrat_48, 0);
  lv_label_set_text(s_clkTime, "--:--");
  lv_obj_align(s_clkTime, LV_ALIGN_CENTER, 0, -SH(5));

  s_clkDate = lv_label_create(s_scrClock);
  lv_obj_set_style_text_color(s_clkDate, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_text_font(s_clkDate, &lv_font_montserrat_20, 0);
  lv_label_set_text(s_clkDate, "");
  lv_obj_align(s_clkDate, LV_ALIGN_CENTER, 0, SH(10));
}

static void updateClock() {
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  if (lt.tm_year + 1900 < 2021) {            // NTP not synced yet
    lv_label_set_text(s_clkTime, "--:--");
    lv_label_set_text(s_clkDate, "syncing");
    return;
  }
  int h12 = lt.tm_hour % 12; if (h12 == 0) h12 = 12;
  char tbuf[8], dbuf[32];
  snprintf(tbuf, sizeof(tbuf), "%d:%02d", h12, lt.tm_min);
  strftime(dbuf, sizeof(dbuf), "%a %b %d", &lt);
  lv_label_set_text(s_clkTime, tbuf);
  lv_label_set_text(s_clkDate, dbuf);
}

// ---------------- input + dispatch ----------------

static void handleNowInput(KnobEvent ev, int32_t d) {
  if (d != 0 && stateLock()) {
    int v = (int)g_player.volume + d;
    v = v < 0 ? 0 : (v > 100 ? 100 : v);
    g_player.volume = (uint8_t)v;
    g_pending.targetVolume = v;
    stateUnlock();
    char b[16];
    snprintf(b, sizeof(b), LV_SYMBOL_VOLUME_MAX " %d", v);
    lv_label_set_text(s_vol, b);
    lv_obj_remove_flag(s_vol, LV_OBJ_FLAG_HIDDEN);
    s_volShownAt = lv_tick_get();
  }
  if (ev == KnobEvent::Short && stateLock()) {
    g_pending.playPause = true;
    g_player.transport = (g_player.transport == TransportState::Playing)
                             ? TransportState::Paused : TransportState::Playing;
    stateUnlock();
  } else if (ev == KnobEvent::Long) {
    uiShow(Screen::Rooms);
  }
}

static void handleRoomsInput(KnobEvent ev, int32_t d) {
  if (d != 0 && !s_roomIps.empty()) {
    int n = (int)s_roomIps.size();
    s_roomSel = (s_roomSel + d) % n;
    if (s_roomSel < 0) s_roomSel += n;
    highlightRoom();
  }
  if (ev == KnobEvent::Short)      selectRoom(s_roomSel);
  else if (ev == KnobEvent::Long)  uiShow(Screen::NowPlaying);  // cancel
}

void uiInit() {
  buildNowPlaying();
  buildRooms();
  buildClock();
  lv_screen_load(s_scrNow);
  s_cur = Screen::NowPlaying;
  s_lastEncBtn = lv_tick_get();
}

void uiTick() {
  KnobEvent ev = knobEvent();
  int32_t   d  = encoderDelta();

  // Combined idle time across encoder/button (manual) and touch (LVGL indev).
  uint32_t now = lv_tick_get();
  if (d != 0 || ev != KnobEvent::None) s_lastEncBtn = now;
  uint32_t idle = lv_display_get_inactive_time(NULL);
  uint32_t encBtnIdle = now - s_lastEncBtn;
  if (encBtnIdle < idle) idle = encBtnIdle;

  // Screensaver: any input while dimmed just wakes (and is consumed, not acted on).
  if (s_cur == Screen::Clock) {
    if (idle < 250) {
      backlightSet(kFullBright);
      uiShow(s_prevScreen);
    } else {
      updateClock();
    }
    lv_timer_handler();
    return;
  }
  if (idle > kSaverMs) {
    s_prevScreen = s_cur;
    updateClock();
    uiShow(Screen::Clock);
    backlightSet(kSaverBright);
    lv_timer_handler();
    return;
  }

  if (s_cur == Screen::NowPlaying) {
    handleNowInput(ev, d);
    const lv_image_dsc_t *dsc = nullptr;
    if (albumArtTake(&dsc)) {
      if (dsc) { lv_image_set_src(s_art, dsc); lv_obj_remove_flag(s_art, LV_OBJ_FLAG_HIDDEN); }
      else     { lv_obj_add_flag(s_art, LV_OBJ_FLAG_HIDDEN); }
    }
    renderNow();
  } else if (s_cur == Screen::Rooms) {
    handleRoomsInput(ev, d);
  }

  lv_timer_handler();
}

void uiShow(Screen s) {
  if (s == s_cur) return;
  if (s == Screen::Rooms)       { populateRooms(); lv_screen_load(s_scrRooms); }
  else if (s == Screen::Clock)  { lv_screen_load(s_scrClock); }
  else                          { lv_screen_load(s_scrNow); }
  s_cur = s;
}
