#include "screens.h"
#include "ui_scale.h"
#include "album_art.h"
#include "../player_state.h"
#include "../sonos/ssdp.h"
#include "../library.h"
#include "../hw/encoder.h"
#include "../hw/pcf8574.h"
#include "../hw/display.h"
#include "../net/ota.h"
#include "../settings.h"
#include <WiFi.h>
#include <lvgl.h>
#include <vector>
#include <time.h>

// Screens (plan §6). NOW PLAYING is home; long-press opens the MENU hub:
//   MENU -> Now Playing / Rooms / Playlists / Favorites
//   list screens: twist=scroll  short press=select  long press=back  (touch a row to pick)
//   inactivity -> CLOCK screensaver; any input wakes.

static Screen s_cur = Screen::NowPlaying;
static bool   s_reqClock    = false;  // swipe-down requested the clock
static bool   s_clockManual = false;  // clock shown manually -> don't auto-leave while playing

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

static void enterBrowse(const char *title, const char *object, int mode);  // fwd decl

// Now-playing gestures: swipe up = queue, swipe down = clock.
static void nowGestureCb(lv_event_t *) {
  lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
  if (dir == LV_DIR_TOP)         enterBrowse("Queue", "Q:0", library::PLAY_QUEUE);
  else if (dir == LV_DIR_BOTTOM) s_reqClock = true;
}

static void buildNowPlaying() {
  s_scrNow = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_scrNow, lv_color_black(), 0);
  lv_obj_remove_flag(s_scrNow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_scrNow, nowGestureCb, LV_EVENT_GESTURE, nullptr);

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

  // Let gestures starting on the centre content bubble up to the screen handler.
  for (lv_obj_t *o : {s_arc, s_art, s_title, s_artist, s_time, s_zone})
    lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);

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
    // Encoder acceleration: faster spins move volume in bigger steps.
    static uint32_t s_lastVolTick = 0;
    uint32_t nowt = lv_tick_get();
    uint32_t dt = nowt - s_lastVolTick;
    s_lastVolTick = nowt;
    int mult = (dt < 35) ? 6 : (dt < 70) ? 3 : (dt < 130) ? 2 : 1;
    int v = (int)g_player.volume + d * mult;
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

static ListScreen s_menu, s_rooms, s_browse, s_group;
static std::vector<String> s_roomIps;     // parallel to the rooms list rows
static std::vector<String> s_groupIps;    // parallel to the group list rows
static std::vector<bool>   s_groupInGroup, s_groupIsActive;
static uint32_t s_groupGen = 0;           // last-seen g_zonesGen, to re-render on change
static int s_browseMode = library::PLAY_FAVORITE;  // how the current browse list plays

// forward decls
static void menuSelect(int idx);
static void roomSelect(int idx);
static void browseSelect(int idx);
static void groupSelect(int idx);

static void menuClickCb(lv_event_t *e)   { s_menu.sel   = (int)(intptr_t)lv_event_get_user_data(e); menuSelect(s_menu.sel); }
static void roomClickCb(lv_event_t *e)   { s_rooms.sel  = (int)(intptr_t)lv_event_get_user_data(e); roomSelect(s_rooms.sel); }
static void browseClickCb(lv_event_t *e) { s_browse.sel = (int)(intptr_t)lv_event_get_user_data(e); browseSelect(s_browse.sel); }
static void groupClickCb(lv_event_t *e)  { s_group.sel  = (int)(intptr_t)lv_event_get_user_data(e); groupSelect(s_group.sel); }

static const char *kMenuItems[] = {"Now Playing", "Rooms", "Group", "Playlists", "Favorites", "Settings"};
static const int   kMenuCount   = 6;

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
  if (labels.empty()) labels.push_back("Searching" LV_SYMBOL_REFRESH);  // discovery not done yet
  listSet(s_rooms, labels, LV_SYMBOL_AUDIO, roomClickCb);
}

static void enterBrowse(const char *title, const char *object, int mode) {
  lv_label_set_text(s_browse.title, title);
  s_browse.sel = 0;
  s_browseMode = mode;
  std::vector<String> loading = {"Loading"};
  listSet(s_browse, loading, LV_SYMBOL_REFRESH, browseClickCb);
  library::requestBrowse(object, mode);
  uiShow(Screen::Playlists);   // shared browse list (playlists / favorites / queue)
}

static void menuSelect(int idx) {
  switch (idx) {
    case 0: uiShow(Screen::NowPlaying); break;
    case 1: uiShow(Screen::Rooms); break;
    case 2: uiShow(Screen::Group); break;
    case 3: enterBrowse("Playlists", "SQ:", library::PLAY_PLAYLIST); break;
    case 4: enterBrowse("Favorites", "FV:2", library::PLAY_FAVORITE); break;
    case 5: uiShow(Screen::Settings); break;
  }
}

static void roomSelect(int idx) {
  if (idx < 0 || idx >= (int)s_roomIps.size()) return;
  if (stateLock()) { g_pending.requestZoneIp = s_roomIps[idx]; stateUnlock(); }
  uiShow(Screen::NowPlaying);
}

// GROUP: every room with a check if it shares the active room's group. Toggling joins a
// room to / removes it from the active group (the active room is the anchor).
static void populateGroup() {
  std::vector<String> labels;
  s_groupIps.clear(); s_groupInGroup.clear(); s_groupIsActive.clear();
  String activeCoord, activeName;
  if (stateLock()) { activeCoord = g_player.coordinatorUuid; activeName = g_player.zoneName; stateUnlock(); }
  const std::vector<sonos::Zone> &zs = sonos::zones();
  for (const auto &z : zs) {
    bool inGroup  = (z.coordinatorUuid == activeCoord);
    bool isActive = (z.name == activeName);
    String label = String(inGroup ? LV_SYMBOL_OK " " : "   ") + z.name + (isActive ? " *" : "");
    labels.push_back(label);
    s_groupIps.push_back(z.ip);
    s_groupInGroup.push_back(inGroup);
    s_groupIsActive.push_back(isActive);
  }
  if (labels.empty()) labels.push_back("Searching" LV_SYMBOL_REFRESH);
  listSet(s_group, labels, LV_SYMBOL_AUDIO, groupClickCb);
}

static void groupSelect(int idx) {
  if (idx < 0 || idx >= (int)s_groupIps.size()) return;
  if (s_groupIsActive[idx]) return;   // can't group/ungroup the anchor itself
  if (stateLock()) {
    if (s_groupInGroup[idx]) g_pending.groupLeaveIp = s_groupIps[idx];
    else                     g_pending.groupJoinIp  = s_groupIps[idx];
    stateUnlock();
  }
  // Stay on the screen; it re-populates when netTask re-discovers (g_zonesGen bumps).
}

static void browseSelect(int idx) {
  library::requestPlay(idx);   // netTask guards the index against the result count
  uiShow(Screen::NowPlaying);
}

// ============================ CLOCK ============================

static lv_obj_t *s_scrClock, *s_clkTime, *s_clkDate;
static Screen    s_prevScreen = Screen::NowPlaying;
static uint32_t  s_lastEncBtn = 0;
static uint32_t  s_clockEnteredMs = 0;   // grace window so the entry touch doesn't wake it
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

// ============================ SETTINGS (brightness + OTA info) ============================

static lv_obj_t *s_scrSettings, *s_setBright, *s_setArc, *s_setOta;
static uint8_t   s_brightness = 100;

static void buildSettings() {
  s_scrSettings = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_scrSettings, lv_color_black(), 0);
  lv_obj_remove_flag(s_scrSettings, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *hdr = lv_label_create(s_scrSettings);
  lv_obj_set_style_text_color(hdr, lv_color_white(), 0);
  lv_obj_set_style_text_font(hdr, &lv_font_montserrat_20, 0);
  lv_label_set_text(hdr, "Settings");
  lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, SH(12));

  s_setArc = lv_arc_create(s_scrSettings);
  lv_obj_set_size(s_setArc, SW(60), SH(60));
  lv_obj_align(s_setArc, LV_ALIGN_CENTER, 0, -SH(4));
  lv_arc_set_rotation(s_setArc, 135);
  lv_arc_set_bg_angles(s_setArc, 0, 270);
  lv_arc_set_range(s_setArc, 10, 100);
  lv_obj_remove_flag(s_setArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_style(s_setArc, nullptr, LV_PART_KNOB);
  lv_obj_set_style_arc_color(s_setArc, lv_palette_main(LV_PALETTE_AMBER), LV_PART_INDICATOR);

  s_setBright = lv_label_create(s_scrSettings);
  lv_obj_set_style_text_color(s_setBright, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_setBright, &lv_font_montserrat_28, 0);
  lv_obj_align(s_setBright, LV_ALIGN_CENTER, 0, -SH(4));

  s_setOta = lv_label_create(s_scrSettings);
  lv_obj_set_style_text_color(s_setOta, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_text_font(s_setOta, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_setOta, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_setOta, LV_ALIGN_CENTER, 0, SH(34));
}

static void showSettings() {
  lv_label_set_text_fmt(s_setBright, LV_SYMBOL_SETTINGS " %d%%", s_brightness);
  lv_arc_set_value(s_setArc, s_brightness);
  lv_label_set_text_fmt(s_setOta, "OTA: sonos-nest\n%s", WiFi.localIP().toString().c_str());
}

static void settingsInput(int32_t d) {
  if (d == 0) return;
  int b = (int)s_brightness + d * 5;
  b = b < 10 ? 10 : (b > 100 ? 100 : b);
  s_brightness = (uint8_t)b;
  backlightSet(s_brightness);
  settingsSetBrightness(s_brightness);
  lv_label_set_text_fmt(s_setBright, LV_SYMBOL_SETTINGS " %d%%", s_brightness);
  lv_arc_set_value(s_setArc, s_brightness);
}

// ============================ OTA overlay ============================

static lv_obj_t *s_scrOta, *s_otaPct, *s_otaBar;

static void buildOta() {
  s_scrOta = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_scrOta, lv_color_black(), 0);
  lv_obj_remove_flag(s_scrOta, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *t = lv_label_create(s_scrOta);   // static title — drawn once, never reflows
  lv_obj_set_style_text_color(t, lv_color_white(), 0);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
  lv_label_set_text(t, "Updating");
  lv_obj_align(t, LV_ALIGN_CENTER, 0, -SH(14));

  // Fixed-width, centre-aligned percent label so it never shifts as the number widens.
  s_otaPct = lv_label_create(s_scrOta);
  lv_obj_set_style_text_color(s_otaPct, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_otaPct, &lv_font_montserrat_28, 0);
  lv_obj_set_width(s_otaPct, SW(60));
  lv_obj_set_style_text_align(s_otaPct, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(s_otaPct, "0%");
  lv_obj_align(s_otaPct, LV_ALIGN_CENTER, 0, -SH(2));

  s_otaBar = lv_bar_create(s_scrOta);
  lv_obj_set_size(s_otaBar, SW(58), SH(4));
  lv_obj_align(s_otaBar, LV_ALIGN_CENTER, 0, SH(8));
  lv_bar_set_range(s_otaBar, 0, 100);
  lv_bar_set_value(s_otaBar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_otaBar, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);
}

// ============================ lifecycle ============================

void uiInit() {
  buildNowPlaying();
  listBuild(s_menu, "Menu");
  listBuild(s_rooms, "Rooms");
  listBuild(s_group, "Group");
  listBuild(s_browse, "Browse");
  buildClock();
  buildSettings();
  buildOta();

  s_brightness = settingsBrightness();

  std::vector<String> menu(kMenuItems, kMenuItems + kMenuCount);
  listSet(s_menu, menu, LV_SYMBOL_LIST, menuClickCb);

  lv_screen_load(s_scrNow);
  s_cur = Screen::NowPlaying;
  s_lastEncBtn = lv_tick_get();
}

void uiTick() {
  // OTA overlay preempts everything during a firmware update.
  if (otaActive()) {
    int p = otaProgress();
    if (p < 0) p = 0;
    uint32_t nowt = lv_tick_get();
    static int      s_otaLastP   = -2;     // raw, for stall detection
    static uint32_t s_otaStallMs = 0;
    static int      s_otaShownP  = -100;   // last painted %
    // Stall safety: no progress for 20s (e.g. upload can't connect back) -> reboot into the
    // still-valid firmware.
    if (p != s_otaLastP) { s_otaLastP = p; s_otaStallMs = nowt; }
    else if (nowt - s_otaStallMs > 20000) ESP.restart();

    bool first = (lv_screen_active() != s_scrOta);
    if (first) { lv_screen_load(s_scrOta); backlightSet(100); }
    // Repaint (one full refresh) only on entry or a >=2% step. Between repaints we never call
    // lv_timer_handler, so the framebuffer stays static and the panel won't tear while the
    // flash write keeps disrupting the CPU cache.
    if (first || p >= 100 || p - s_otaShownP >= 2) {
      s_otaShownP = p;
      lv_label_set_text_fmt(s_otaPct, "%d%%", p);
      lv_bar_set_value(s_otaBar, p, LV_ANIM_OFF);
      lv_timer_handler();
    }
    return;
  }

  KnobEvent ev = knobEvent();
  int32_t   d  = encoderDelta();

  uint32_t now = lv_tick_get();
  if (d != 0 || ev != KnobEvent::None) s_lastEncBtn = now;
  uint32_t idle = lv_display_get_inactive_time(NULL);
  uint32_t encBtnIdle = now - s_lastEncBtn;
  if (encBtnIdle < idle) idle = encBtnIdle;

  bool playing = false;
  if (stateLock()) { playing = (g_player.transport == TransportState::Playing); stateUnlock(); }

  // Manual clock via swipe-down on now-playing (works even while a track is playing).
  if (s_reqClock) {
    s_reqClock = false;
    if (s_cur == Screen::NowPlaying) {
      s_clockManual = true;
      s_prevScreen = Screen::NowPlaying;
      s_clockEnteredMs = now;
      updateClock();
      uiShow(Screen::Clock);
      backlightSet(kSaverBright);
      lv_timer_handler();
      return;
    }
  }

  if (s_cur == Screen::Clock) {
    // Wake on any input (after a short grace so the entry swipe doesn't wake it instantly).
    if (idle < 250 && lv_tick_elaps(s_clockEnteredMs) > 600) {
      backlightSet(s_brightness);
      s_clockManual = false;
      uiShow(playing ? Screen::NowPlaying : s_prevScreen);
    } else if (!s_clockManual && playing) {
      // Idle screensaver only: if music starts, leave the clock. (Manual clock stays.)
      backlightSet(s_brightness);
      uiShow(Screen::NowPlaying);
    } else {
      updateClock();
    }
    lv_timer_handler();
    return;
  }
  // Idle screensaver — only when nothing is playing.
  if (idle > kSaverMs && !playing) {
    s_prevScreen = s_cur;
    s_clockManual = false;
    s_clockEnteredMs = now;
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
      else if (ev == KnobEvent::Long)
        uiShow(s_browseMode == library::PLAY_QUEUE ? Screen::NowPlaying : Screen::Home);
      break;
    }
    case Screen::Group:
      if (g_zonesGen != s_groupGen) { s_groupGen = g_zonesGen; populateGroup(); }
      if (d) listMove(s_group, d > 0 ? 1 : -1);
      if (ev == KnobEvent::Short)     groupSelect(s_group.sel);
      else if (ev == KnobEvent::Long) uiShow(Screen::Home);
      break;
    case Screen::Settings:
      settingsInput(d);
      if (ev == KnobEvent::Long) uiShow(Screen::Home);
      break;
    default: break;
  }

  lv_timer_handler();
}

void uiShow(Screen s) {
  if (s == s_cur) return;
  // Leaving the browse list: free its rows back to the LVGL pool and drop the cached items,
  // so a big queue doesn't starve later allocations (e.g. the scaled clock's draw layer).
  if (s_cur == Screen::Playlists && s != Screen::Playlists) {
    lv_obj_clean(s_browse.list);
    library::clearResults();
  }
  switch (s) {
    case Screen::Home:      lv_screen_load(s_menu.scr);   break;
    case Screen::Rooms:     populateRooms(); lv_screen_load(s_rooms.scr); break;
    case Screen::Group:     populateGroup(); s_groupGen = g_zonesGen; lv_screen_load(s_group.scr); break;
    case Screen::Playlists: lv_screen_load(s_browse.scr); break;
    case Screen::Settings:  showSettings(); lv_screen_load(s_scrSettings); break;
    case Screen::Clock:     lv_screen_load(s_scrClock);   break;
    default:                lv_screen_load(s_scrNow);     break;
  }
  s_cur = s;
}
