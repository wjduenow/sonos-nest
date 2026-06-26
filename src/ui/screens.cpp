#include "screens.h"
#include "ui_scale.h"
#include "../player_state.h"
#include "../hw/encoder.h"
#include "../hw/pcf8574.h"
#include <lvgl.h>

// NOW PLAYING screen (plan §6): progress arc around the rim, title/artist in the middle,
// play/pause state, a transient volume readout, and prev/next touch buttons.
//   twist      = volume +/-   (optimistic; net_task sends SetVolume)
//   short push = play/pause
//   touch prev/next buttons = previous/next track
// TODO Phase 2b: album art image behind the text (TJpg decode + cache on track change).
// TODO Phase 3: PLAYLISTS / RADIO browse lists.  TODO Phase 4: ROOMS / CLOCK.

static lv_obj_t *s_arc;
static lv_obj_t *s_title;
static lv_obj_t *s_artist;
static lv_obj_t *s_state;     // play/pause glyph
static lv_obj_t *s_time;      // "1:23 / 4:56"
static lv_obj_t *s_zone;      // room name
static lv_obj_t *s_vol;       // transient volume overlay
static uint32_t  s_volShownAt;

static void fmtTime(char *buf, size_t n, uint32_t sec) {
  snprintf(buf, n, "%lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
}

static void prevCb(lv_event_t *) { if (stateLock()) { g_pending.prev = true; stateUnlock(); } }
static void nextCb(lv_event_t *) { if (stateLock()) { g_pending.next = true; stateUnlock(); } }

static lv_obj_t *makeNavBtn(lv_obj_t *scr, const char *sym, lv_align_t align,
                            lv_event_cb_t cb) {
  lv_obj_t *b = lv_button_create(scr);
  lv_obj_set_size(b, SW(16), SW(16));
  lv_obj_align(b, align, align == LV_ALIGN_LEFT_MID ? SW(2) : -SW(2), 0);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_20, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, sym);
  lv_obj_center(l);
  return b;
}

void uiInit() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // Progress arc around the rim (indicator only — not user-draggable).
  s_arc = lv_arc_create(scr);
  lv_obj_set_size(s_arc, SW(96), SH(96));
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

  s_zone = lv_label_create(scr);
  lv_obj_set_style_text_color(s_zone, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_text_font(s_zone, &lv_font_montserrat_14, 0);
  lv_label_set_text(s_zone, "");
  lv_obj_align(s_zone, LV_ALIGN_CENTER, 0, -SH(24));

  s_state = lv_label_create(scr);
  lv_obj_set_style_text_color(s_state, lv_color_white(), 0);
  lv_label_set_text(s_state, LV_SYMBOL_STOP);
  lv_obj_align(s_state, LV_ALIGN_CENTER, 0, -SH(14));

  s_title = lv_label_create(scr);
  lv_obj_set_style_text_color(s_title, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(s_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(s_title, SW(64));
  lv_label_set_text(s_title, "—");
  lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

  s_artist = lv_label_create(scr);
  lv_obj_set_style_text_color(s_artist, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_text_font(s_artist, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_align(s_artist, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(s_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(s_artist, SW(64));
  lv_label_set_text(s_artist, "");
  lv_obj_align(s_artist, LV_ALIGN_CENTER, 0, SH(16));

  s_time = lv_label_create(scr);
  lv_obj_set_style_text_color(s_time, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_text_font(s_time, &lv_font_montserrat_14, 0);
  lv_label_set_text(s_time, "0:00 / 0:00");
  lv_obj_align(s_time, LV_ALIGN_CENTER, 0, SH(28));

  // Transient volume overlay (hidden until the knob is turned).
  s_vol = lv_label_create(scr);
  lv_obj_set_style_text_color(s_vol, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_vol, &lv_font_montserrat_28, 0);
  lv_obj_align(s_vol, LV_ALIGN_CENTER, 0, SH(36));
  lv_obj_add_flag(s_vol, LV_OBJ_FLAG_HIDDEN);

  makeNavBtn(scr, LV_SYMBOL_PREV, LV_ALIGN_LEFT_MID, prevCb);
  makeNavBtn(scr, LV_SYMBOL_NEXT, LV_ALIGN_RIGHT_MID, nextCb);
}

static void handleInput() {
  int32_t d = encoderDelta();
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

  if (knobPressed() && stateLock()) {
    g_pending.playPause = true;
    // Optimistic feedback: flip the displayed state immediately so a press is visible even
    // before the speaker responds (the next poll corrects it to the real state).
    g_player.transport = (g_player.transport == TransportState::Playing)
                             ? TransportState::Paused
                             : TransportState::Playing;
    stateUnlock();
  }
}

static void render() {
  // Snapshot shared state under the lock, then touch LVGL outside it.
  TransportState st;
  uint32_t pos, dur;
  char title[128], artist[128], zone[40];
  if (!stateLock()) return;
  st  = g_player.transport;
  pos = g_player.positionSec;
  dur = g_player.durationSec;
  snprintf(title,  sizeof(title),  "%s", g_player.title.length()  ? g_player.title.c_str()  : "—");
  snprintf(artist, sizeof(artist), "%s", g_player.artist.c_str());
  snprintf(zone,   sizeof(zone),   "%s", g_player.zoneName.c_str());
  stateUnlock();

  lv_label_set_text(s_title, title);
  lv_label_set_text(s_artist, artist);
  lv_label_set_text(s_zone, zone);
  lv_label_set_text(s_state,
                    st == TransportState::Playing ? LV_SYMBOL_PLAY :
                    st == TransportState::Paused  ? LV_SYMBOL_PAUSE : LV_SYMBOL_STOP);

  lv_arc_set_value(s_arc, dur ? (int32_t)((uint64_t)pos * 1000 / dur) : 0);
  char tbuf[24], a[8], b[8];
  fmtTime(a, sizeof(a), pos);
  fmtTime(b, sizeof(b), dur);
  snprintf(tbuf, sizeof(tbuf), "%s / %s", a, b);
  lv_label_set_text(s_time, tbuf);

  if (!lv_obj_has_flag(s_vol, LV_OBJ_FLAG_HIDDEN) && lv_tick_elaps(s_volShownAt) > 1500)
    lv_obj_add_flag(s_vol, LV_OBJ_FLAG_HIDDEN);
}

void uiTick() {
  handleInput();
  render();
  lv_timer_handler();
}

void uiShow(Screen) {
  // TODO Phase 3+: switch the active screen.
}
