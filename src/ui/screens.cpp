#include "screens.h"
#include "ui_scale.h"
#include "album_art.h"
#include "../player_state.h"
#include "../sonos/ssdp.h"
#include "../library.h"
#include "../hw/encoder.h"
#include "../hw/pcf8574.h"
#include "../hw/display.h"
#include <lvgl.h>
#include <vector>
#include <time.h>

// Screens (plan §6). NOW PLAYING is home; long-press opens the MENU hub:
//   MENU -> Now Playing / Rooms / Playlists / Favorites
//   list screens: twist=scroll  short press=select  long press=back  (touch a row to pick)
//   inactivity -> CLOCK screensaver; any input wakes.

static Screen s_cur = Screen::NowPlaying;

// ============================ generic list screen ============================

struct ListScreen {
  lv_obj_t *scr   = nullptr;
  lv_obj_t *title = nullptr;
  lv_obj_t *list  = nullptr;
  int       sel   = 0;
};

static void listHighlight(ListScreen &L) {
  uint32_t n = lv_obj_get_child_count(L.list);
  for (uint32_t i = 0; i < n; ++i) {
    lv_obj_t *b = lv_obj_get_child(L.list, i);
    bool s = ((int)i == L.sel);
    lv_obj_set_style_bg_opa(b, s ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(b, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_text_color(b, lv_color_white(), 0);
    lv_obj_set_style_text_font(b, &lv_font_montserrat_20, 0);
    if (s) lv_obj_scroll_to_view(b, LV_ANIM_ON);
  }
}

static void listBuild(ListScreen &L, const char *titleText) {
  L.scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(L.scr, lv_color_black(), 0);
  lv_obj_remove_flag(L.scr, LV_OBJ_FLAG_SCROLLABLE);

  L.title = lv_label_create(L.scr);
  lv_obj_set_style_text_color(L.title, lv_color_white(), 0);
  lv_obj_set_style_text_font(L.title, &lv_font_montserrat_20, 0);
  lv_label_set_text(L.title, titleText);
  lv_obj_align(L.title, LV_ALIGN_TOP_MID, 0, SH(12));

  L.list = lv_list_create(L.scr);
  lv_obj_set_size(L.list, SW(80), SH(64));
  lv_obj_align(L.list, LV_ALIGN_CENTER, 0, SH(6));
  lv_obj_set_style_bg_opa(L.list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(L.list, 0, 0);
}

static void listSet(ListScreen &L, const std::vector<String> &labels, const char *icon,
                    lv_event_cb_t clickCb) {
  lv_obj_clean(L.list);
  for (size_t i = 0; i < labels.size(); ++i) {
    lv_obj_t *btn = lv_list_add_button(L.list, icon, labels[i].c_str());
    lv_obj_add_event_cb(btn, clickCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  if (L.sel >= (int)labels.size()) L.sel = 0;
  listHighlight(L);
}

static void listMove(ListScreen &L, int delta) {
  int n = (int)lv_obj_get_child_count(L.list);
  if (n <= 0) return;
  L.sel = (L.sel + delta) % n;
  if (L.sel < 0) L.sel += n;
  listHighlight(L);
}

// ============================ NOW PLAYING ============================

static lv_obj_t *s_scrNow;
static lv_obj_t *s_arc, *s_art, *s_title, *s_artist, *s_time, *s_zone, *s_vol, *s_ripple;
static uint32_t  s_volShownAt;

// Subtle press feedback: a white ring that expands from the centre and fades out.
static void rippleExec(void *obj, int32_t v) {  // v: 0..255
  lv_obj_t *o = (lv_obj_t *)obj;
  lv_coord_t sz = SW(8) + SW(52) * v / 255;
  lv_obj_set_size(o, sz, sz);
  lv_obj_center(o);
  lv_obj_set_style_border_opa(o, (lv_opa_t)(LV_OPA_70 - LV_OPA_70 * v / 255), 0);
}
static void rippleDone(lv_anim_t *) { lv_obj_add_flag(s_ripple, LV_OBJ_FLAG_HIDDEN); }

static void pressPulse() {
  lv_obj_remove_flag(s_ripple, LV_OBJ_FLAG_HIDDEN);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_ripple);
  lv_anim_set_exec_cb(&a, rippleExec);
  lv_anim_set_values(&a, 0, 255);
  lv_anim_set_duration(&a, 280);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_completed_cb(&a, rippleDone);
  lv_anim_start(&a);
}

static void fmtTime(char *buf, size_t n, uint32_t sec) {
  snprintf(buf, n, "%lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
}

static void prevCb(lv_event_t *) { if (stateLock()) { g_pending.prev = true; stateUnlock(); } pressPulse(); }
static void nextCb(lv_event_t *) { if (stateLock()) { g_pending.next = true; stateUnlock(); } pressPulse(); }

static lv_obj_t *makeNavBtn(lv_obj_t *scr, const char *sym, lv_align_t align, lv_event_cb_t cb) {
  lv_obj_t *b = lv_button_create(scr);
  lv_obj_set_size(b, SW(23), SW(23));   // ~110px tap target
  // Inset from the rim so the button clears the progress arc.
  lv_obj_align(b, align, align == LV_ALIGN_LEFT_MID ? SW(7) : -SW(7), 0);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_40, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = lv_label_create(b);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);   // bigger arrow glyph
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
  lv_obj_set_style_text_font(s_zone, &lv_font_montserrat_20, 0);
  lv_label_set_text(s_zone, "");
  lv_obj_align(s_zone, LV_ALIGN_CENTER, 0, -SH(32));

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
  lv_obj_set_style_text_font(s_artist, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_align(s_artist, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(s_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(s_artist, SW(70));
  lv_label_set_text(s_artist, "");
  lv_obj_align(s_artist, LV_ALIGN_CENTER, 0, SH(23));

  s_time = lv_label_create(s_scrNow);
  lv_obj_set_style_text_color(s_time, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_text_font(s_time, &lv_font_montserrat_20, 0);
  lv_label_set_text(s_time, LV_SYMBOL_STOP " 0:00 / 0:00");
  lv_obj_align(s_time, LV_ALIGN_CENTER, 0, SH(33));

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

  s_ripple = lv_obj_create(s_scrNow);
  lv_obj_remove_flag(s_ripple, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s_ripple, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(s_ripple, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(s_ripple, lv_color_white(), 0);
  lv_obj_set_style_border_width(s_ripple, 3, 0);
  lv_obj_set_style_radius(s_ripple, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_size(s_ripple, SW(8), SW(8));
  lv_obj_center(s_ripple);
  lv_obj_add_flag(s_ripple, LV_OBJ_FLAG_HIDDEN);
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
    // Decide the explicit action from the real (pre-flip) state, then flip optimistically.
    bool wasPlaying = (g_player.transport == TransportState::Playing);
    g_pending.setPlay  = wasPlaying ? 0 : 1;
    g_player.transport = wasPlaying ? TransportState::Paused : TransportState::Playing;
    stateUnlock();
    pressPulse();   // subtle on-screen press feedback
  } else if (ev == KnobEvent::Long) {
    uiShow(Screen::Home);
  }
}

// ============================ MENU / ROOMS / BROWSE ============================

static ListScreen s_menu, s_rooms, s_browse;
static std::vector<String> s_roomIps;   // parallel to the rooms list rows
static bool s_browseQueue = false;      // current browse: true=playlist queue flow

// forward decls
static void menuSelect(int idx);
static void roomSelect(int idx);
static void browseSelect(int idx);

static void menuClickCb(lv_event_t *e)   { s_menu.sel   = (int)(intptr_t)lv_event_get_user_data(e); menuSelect(s_menu.sel); }
static void roomClickCb(lv_event_t *e)   { s_rooms.sel  = (int)(intptr_t)lv_event_get_user_data(e); roomSelect(s_rooms.sel); }
static void browseClickCb(lv_event_t *e) { s_browse.sel = (int)(intptr_t)lv_event_get_user_data(e); browseSelect(s_browse.sel); }

static const char *kMenuItems[] = {"Now Playing", "Rooms", "Playlists", "Favorites"};

static void populateRooms() {
  std::vector<String> labels;
  s_roomIps.clear();
  String cur;
  if (stateLock()) { cur = g_player.zoneName; stateUnlock(); }
  const std::vector<sonos::Zone> &zs = sonos::zones();
  for (size_t i = 0; i < zs.size(); ++i) {
    labels.push_back(zs[i].name);
    s_roomIps.push_back(zs[i].ip);
    if (zs[i].name == cur) s_rooms.sel = (int)i;
  }
  listSet(s_rooms, labels, LV_SYMBOL_AUDIO, roomClickCb);
}

static void enterBrowse(const char *title, const char *object, bool queueFlow) {
  lv_label_set_text(s_browse.title, title);
  s_browse.sel = 0;
  s_browseQueue = queueFlow;
  std::vector<String> loading = {"Loading"};
  listSet(s_browse, loading, LV_SYMBOL_REFRESH, browseClickCb);
  library::requestBrowse(object, queueFlow);
  uiShow(Screen::Playlists);   // shared browse list (playlists + favorites)
}

static void menuSelect(int idx) {
  switch (idx) {
    case 0: uiShow(Screen::NowPlaying); break;
    case 1: uiShow(Screen::Rooms); break;
    case 2: enterBrowse("Playlists", "SQ:", true); break;
    case 3: enterBrowse("Favorites", "FV:2", false); break;
  }
}

static void roomSelect(int idx) {
  if (idx < 0 || idx >= (int)s_roomIps.size()) return;
  if (stateLock()) { g_pending.requestZoneIp = s_roomIps[idx]; stateUnlock(); }
  uiShow(Screen::NowPlaying);
}

static void browseSelect(int idx) {
  library::requestPlay(idx);   // netTask guards the index against the result count
  uiShow(Screen::NowPlaying);
}

// ============================ CLOCK ============================

static lv_obj_t *s_scrClock, *s_clkTime, *s_clkDate;
static Screen    s_prevScreen = Screen::NowPlaying;
static uint32_t  s_lastEncBtn = 0;
static const uint32_t kSaverMs    = 30000;
static const uint8_t  kSaverBright = 12;
static const uint8_t  kFullBright  = 100;

static void buildClock() {
  s_scrClock = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_scrClock, lv_color_black(), 0);
  lv_obj_remove_flag(s_scrClock, LV_OBJ_FLAG_SCROLLABLE);

  // 256 = 1.0x. LVGL has no >48px built-in font, so scale the labels up via transform.
  s_clkTime = lv_label_create(s_scrClock);
  lv_obj_set_style_text_color(s_clkTime, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_clkTime, &lv_font_montserrat_48, 0);
  lv_obj_set_style_transform_pivot_x(s_clkTime, lv_pct(50), 0);
  lv_obj_set_style_transform_pivot_y(s_clkTime, lv_pct(50), 0);
  lv_obj_set_style_transform_scale_x(s_clkTime, 720, 0);   // ~2.8x -> ~135px tall
  lv_obj_set_style_transform_scale_y(s_clkTime, 720, 0);
  lv_label_set_text(s_clkTime, "--:--");
  lv_obj_align(s_clkTime, LV_ALIGN_CENTER, 0, -SH(8));

  s_clkDate = lv_label_create(s_scrClock);
  lv_obj_set_style_text_color(s_clkDate, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_text_font(s_clkDate, &lv_font_montserrat_28, 0);
  lv_obj_set_style_transform_pivot_x(s_clkDate, lv_pct(50), 0);
  lv_obj_set_style_transform_pivot_y(s_clkDate, lv_pct(50), 0);
  lv_obj_set_style_transform_scale_x(s_clkDate, 384, 0);   // ~1.5x -> ~42px
  lv_obj_set_style_transform_scale_y(s_clkDate, 384, 0);
  lv_label_set_text(s_clkDate, "");
  lv_obj_align(s_clkDate, LV_ALIGN_CENTER, 0, SH(24));
}

static void updateClock() {
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  if (lt.tm_year + 1900 < 2021) {
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

// ============================ lifecycle ============================

void uiInit() {
  buildNowPlaying();
  listBuild(s_menu, "Menu");
  listBuild(s_rooms, "Rooms");
  listBuild(s_browse, "Browse");
  buildClock();

  std::vector<String> menu(kMenuItems, kMenuItems + 4);
  listSet(s_menu, menu, LV_SYMBOL_LIST, menuClickCb);

  lv_screen_load(s_scrNow);
  s_cur = Screen::NowPlaying;
  s_lastEncBtn = lv_tick_get();
}

void uiTick() {
  KnobEvent ev = knobEvent();
  int32_t   d  = encoderDelta();

  uint32_t now = lv_tick_get();
  if (d != 0 || ev != KnobEvent::None) s_lastEncBtn = now;
  uint32_t idle = lv_display_get_inactive_time(NULL);
  uint32_t encBtnIdle = now - s_lastEncBtn;
  if (encBtnIdle < idle) idle = encBtnIdle;

  // Screensaver only when nothing is playing; if music starts, leave the clock.
  bool playing = false;
  if (stateLock()) { playing = (g_player.transport == TransportState::Playing); stateUnlock(); }

  if (s_cur == Screen::Clock) {
    if (idle < 250 || playing) {
      backlightSet(kFullBright);
      uiShow(playing ? Screen::NowPlaying : s_prevScreen);
    } else {
      updateClock();
    }
    lv_timer_handler();
    return;
  }
  if (idle > kSaverMs && !playing) {
    s_prevScreen = s_cur;
    updateClock();
    uiShow(Screen::Clock);
    backlightSet(kSaverBright);
    lv_timer_handler();
    return;
  }

  switch (s_cur) {
    case Screen::NowPlaying: {
      handleNowInput(ev, d);
      const lv_image_dsc_t *dsc = nullptr;
      if (albumArtTake(&dsc)) {
        if (dsc) { lv_image_set_src(s_art, dsc); lv_obj_remove_flag(s_art, LV_OBJ_FLAG_HIDDEN); }
        else     { lv_obj_add_flag(s_art, LV_OBJ_FLAG_HIDDEN); }
      }
      renderNow();
      break;
    }
    case Screen::Home:
      if (d) listMove(s_menu, d > 0 ? 1 : -1);
      if (ev == KnobEvent::Short)     menuSelect(s_menu.sel);
      else if (ev == KnobEvent::Long) uiShow(Screen::NowPlaying);
      break;
    case Screen::Rooms:
      if (d) listMove(s_rooms, d > 0 ? 1 : -1);
      if (ev == KnobEvent::Short)     roomSelect(s_rooms.sel);
      else if (ev == KnobEvent::Long) uiShow(Screen::Home);
      break;
    case Screen::Playlists: {       // shared browse list (playlists + favorites)
      std::vector<String> labels;
      if (library::takeResults(labels)) {
        if (labels.empty()) labels.push_back("(empty)");
        listSet(s_browse, labels, LV_SYMBOL_AUDIO, browseClickCb);
      }
      if (d) listMove(s_browse, d > 0 ? 1 : -1);
      if (ev == KnobEvent::Short)     browseSelect(s_browse.sel);
      else if (ev == KnobEvent::Long) uiShow(Screen::Home);
      break;
    }
    default: break;
  }

  lv_timer_handler();
}

void uiShow(Screen s) {
  if (s == s_cur) return;
  switch (s) {
    case Screen::Home:      lv_screen_load(s_menu.scr);   break;
    case Screen::Rooms:     populateRooms(); lv_screen_load(s_rooms.scr); break;
    case Screen::Playlists: lv_screen_load(s_browse.scr); break;
    case Screen::Clock:     lv_screen_load(s_scrClock);   break;
    default:                lv_screen_load(s_scrNow);     break;
  }
  s_cur = s;
}
