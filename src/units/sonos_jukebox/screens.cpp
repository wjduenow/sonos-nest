// UX for sonos-jukebox — the wall-mounted 7" controller. Implements core/unit.h.
//
// Laid out from the design system (`.claude/skills/sonos-jukebox-design/`), specifically
// `ui_kits/jukebox-screen/{App,NowPlaying}.jsx` and the component specs under `components/`.
// Read those before changing geometry, and prefer the tokens in ui_scale.h over ad-hoc values.
//
// Deviations from the design, all deliberate:
//   - Lucide icons -> LVGL's built-in symbol font. No icon set is converted for the device yet;
//     LV_SYMBOL_* is the closest equivalent and costs nothing.
//   - JetBrains Mono -> Montserrat. LVGL ships no mono face, so timecodes jitter slightly as
//     digits change width. A converted mono digit subset is the proper fix (see the TODO below).
//   - Type sizes snap to the nearest enabled Montserrat (52->48, 15->16, 13/10->12).
//
// House rule (CLAUDE.md > Architecture): the UI never calls Sonos/SOAP or board pins directly. It
// reads the mutex-guarded g_player, posts work to g_pending, and reaches hardware only through
// core/board.h.
#include <Arduino.h>
#include <lvgl.h>

#include "core/album_art.h"
#include "core/board.h"
#include "core/player_state.h"
#include "core/unit.h"
#include "ui_scale.h"

// --- Geometry, from the design's device shell -------------------------------------------------
static const lv_coord_t RAIL_W    = 66;    // left nav rail (App.jsx)
static const lv_coord_t PAD_X     = 30;    // content gutter (NowPlaying.jsx padding 22px 30px 18px)
static const lv_coord_t PAD_TOP   = 22;
static const lv_coord_t PAD_BOT   = 18;
static const lv_coord_t ART       = 280;   // album art tile, radius --r-lg
static const lv_coord_t GAP       = 34;    // art -> text column

static lv_obj_t *s_content = nullptr;
static lv_obj_t *s_provisioning = nullptr;

// Status bar
static lv_obj_t *s_dot = nullptr, *s_room = nullptr, *s_group = nullptr, *s_clock = nullptr;
// Now playing
static lv_obj_t *s_art = nullptr, *s_artImg = nullptr, *s_artPh = nullptr, *s_badgeSrc = nullptr, *s_title = nullptr, *s_meta = nullptr;
static lv_obj_t *s_elapsed = nullptr, *s_remain = nullptr, *s_track = nullptr, *s_fill = nullptr;
// Transport + volume
static lv_obj_t *s_play = nullptr, *s_playLbl = nullptr;
static lv_obj_t *s_volIcon = nullptr, *s_volFill = nullptr, *s_volPct = nullptr;

static bool s_wasPlaying = false;

// Last-rendered values. LVGL's setters do NOT compare — lv_label_set_text() copies and
// invalidates unconditionally — so calling them every tick redraws those regions every frame.
// With LV_DISPLAY_RENDER_MODE_DIRECT there is a single frame buffer that the DSI is scanning out
// live, so a needless redraw is visible as flicker/tearing. Only touch a widget when its value
// has actually changed.
struct Shown {
  String   room, title, meta, badge;
  int      pct = -1, volPct = -1;
  uint32_t elapsed = UINT32_MAX, remain = UINT32_MAX;
  int      playing = -1;      // tri-state so the first tick always paints
  uint8_t  volIcon = 0xFF;
};
static Shown s_shown;

static inline void setTextIfChanged(lv_obj_t *l, String &cache, const String &next) {
  if (cache == next) return;
  cache = next;
  lv_label_set_text(l, next.c_str());
}

// --- Commands ---------------------------------------------------------------------------------
static void prevCb(lv_event_t *) { if (stateLock()) { g_pending.prev = true; stateUnlock(); } }
static void nextCb(lv_event_t *) { if (stateLock()) { g_pending.next = true; stateUnlock(); } }
static void playCb(lv_event_t *) {
  // Decide from the last rendered state, exactly as the nest does: the UI owns the intent, the
  // net task owns the SOAP call.
  if (stateLock()) { g_pending.setPlay = s_wasPlaying ? 0 : 1; stateUnlock(); }
}

// --- Small builders ---------------------------------------------------------------------------
static lv_obj_t *panel(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, uint32_t bg, lv_coord_t r) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, r, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static lv_obj_t *label(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t colour) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
  return l;
}

// Round transport control. `solid` is the design's accent-filled primary — reserved for
// play/pause; everything else is `ghost`.
static lv_obj_t *transportBtn(lv_obj_t *parent, const char *sym, lv_coord_t d, bool solid,
                              lv_event_cb_t cb, lv_obj_t **labelOut = nullptr) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, d, d);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(b, solid ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(JB_ACCENT), 0);
  // Touch is the only input on this board until the physical caps are wired, so the pressed
  // state has to be unmistakable.
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(b, lv_color_hex(solid ? JB_ACCENT_INK : JB_SCREEN_ELEV_2),
                            LV_STATE_PRESSED);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *l = label(b, sym, &lv_font_montserrat_24, solid ? JB_ACCENT_INK : JB_TEXT_MUTED);
  lv_obj_center(l);
  if (labelOut) *labelOut = l;
  return b;
}

// --- Screen -----------------------------------------------------------------------------------
static void buildRail(lv_obj_t *scr) {
  lv_obj_t *rail = panel(scr, RAIL_W, SCREEN_H, JB_SCREEN_BG, 0);
  lv_obj_align(rail, LV_ALIGN_TOP_LEFT, 0, 0);
  // Hairline divider — the design uses a 1px --screen-line border, not a filled panel.
  lv_obj_t *line = panel(scr, 1, SCREEN_H, JB_SCREEN_LINE, 0);
  lv_obj_align(line, LV_ALIGN_TOP_LEFT, RAIL_W, 0);

  // Only Now Playing exists so far; Radio and Rooms are placeholders so the rail reads as the
  // designed 3-item nav rather than appearing broken.
  const char *icons[3] = {LV_SYMBOL_AUDIO, LV_SYMBOL_LIST, LV_SYMBOL_VOLUME_MAX};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *b = panel(scr, 48, 48, i == 0 ? JB_SCREEN_ELEV_2 : JB_SCREEN_BG, JB_R_MD);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, (RAIL_W - 48) / 2, PAD_TOP + i * 58);
    lv_obj_t *l = label(b, icons[i], &lv_font_montserrat_20, i == 0 ? JB_ACCENT : JB_TEXT_DIM);
    lv_obj_center(l);
  }
}

static void buildStatusBar() {
  s_dot = panel(s_content, 9, 9, JB_ACCENT, LV_RADIUS_CIRCLE);
  lv_obj_align(s_dot, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 6);

  s_room = label(s_content, "—", &lv_font_montserrat_16, JB_TEXT);
  lv_obj_align(s_room, LV_ALIGN_TOP_LEFT, 18, PAD_TOP);

  s_group = label(s_content, "", &lv_font_montserrat_12, JB_TEXT_DIM);
  lv_obj_align(s_group, LV_ALIGN_TOP_LEFT, 18, PAD_TOP + 20);

  s_clock = label(s_content, LV_SYMBOL_WIFI, &lv_font_montserrat_16, JB_TEXT_MUTED);
  lv_obj_align(s_clock, LV_ALIGN_TOP_RIGHT, 0, PAD_TOP);
}

static void buildNowPlaying() {
  // Album art. Solid --screen-elev until core/album_art delivers a real cover; the design uses a
  // rounded tile with a hairline, never a bare rectangle.
  s_art = panel(s_content, ART, ART, JB_SCREEN_ELEV, JB_R_LG);
  lv_obj_align(s_art, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_border_width(s_art, 1, 0);
  lv_obj_set_style_border_color(s_art, lv_color_hex(JB_SCREEN_LINE), 0);
  s_artPh = label(s_art, LV_SYMBOL_AUDIO, &lv_font_montserrat_48, JB_SCREEN_LINE);
  lv_obj_center(s_artPh);

  // Real cover, decoded by the art task into a PSRAM buffer (core/album_art). Sits above the
  // placeholder and is revealed only once there is something to show. TJpg picks a power-of-2
  // downscale so the long edge fits ART_MAX_PX (280 here), which means the decoded image is
  // often smaller than the tile — centre it rather than stretching.
  s_artImg = lv_image_create(s_art);
  lv_obj_center(s_artImg);
  lv_obj_add_flag(s_artImg, LV_OBJ_FLAG_HIDDEN);

  const lv_coord_t textX = ART + GAP;
  const lv_coord_t textW = SCREEN_W - RAIL_W - PAD_X * 2 - textX;

  s_badgeSrc = label(s_content, "", &lv_font_montserrat_12, JB_ACCENT);
  lv_obj_align(s_badgeSrc, LV_ALIGN_LEFT_MID, textX, -118);

  s_title = label(s_content, "Sonos Jukebox", &lv_font_montserrat_48, JB_TEXT);
  lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_title, textW);
  lv_obj_align(s_title, LV_ALIGN_LEFT_MID, textX, -64);

  s_meta = label(s_content, "starting up", &lv_font_montserrat_22, JB_TEXT_MUTED);
  lv_label_set_long_mode(s_meta, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_meta, textW);
  lv_obj_align(s_meta, LV_ALIGN_LEFT_MID, textX, -8);

  // Scrubber: 6px track, accent fill, mono-ish timecodes beneath.
  s_track = panel(s_content, textW, 6, JB_SCREEN_ELEV_2, 3);
  lv_obj_align(s_track, LV_ALIGN_LEFT_MID, textX, 48);
  s_fill = panel(s_track, 0, 6, JB_ACCENT, 3);
  lv_obj_align(s_fill, LV_ALIGN_LEFT_MID, 0, 0);

  s_elapsed = label(s_content, "0:00", &lv_font_montserrat_12, JB_TEXT_DIM);
  lv_obj_align(s_elapsed, LV_ALIGN_LEFT_MID, textX, 68);
  s_remain = label(s_content, "-0:00", &lv_font_montserrat_12, JB_TEXT_DIM);
  lv_obj_align(s_remain, LV_ALIGN_LEFT_MID, textX + textW - 44, 68);
}

static void buildTransport() {
  // Bottom row: volume left, transport right (design: VolumeBar flex + button cluster gap 14).
  const lv_coord_t rowY = -PAD_BOT;

  s_volIcon = label(s_content, LV_SYMBOL_VOLUME_MAX, &lv_font_montserrat_20, JB_TEXT_MUTED);
  lv_obj_align(s_volIcon, LV_ALIGN_BOTTOM_LEFT, 0, rowY - 12);

  lv_obj_t *volTrack = panel(s_content, 260, 6, JB_SCREEN_ELEV_2, 3);
  lv_obj_align(volTrack, LV_ALIGN_BOTTOM_LEFT, 34, rowY - 18);
  s_volFill = panel(volTrack, 0, 6, JB_ACCENT, 3);
  lv_obj_align(s_volFill, LV_ALIGN_LEFT_MID, 0, 0);

  s_volPct = label(s_content, "0", &lv_font_montserrat_12, JB_TEXT_MUTED);
  lv_obj_align(s_volPct, LV_ALIGN_BOTTOM_LEFT, 306, rowY - 14);

  // 44px is the design system's --hit-min. These are the ONLY transport controls until the
  // physical caps are wired, so they are sized generously rather than as a secondary affordance.
  lv_obj_t *prev = transportBtn(s_content, LV_SYMBOL_PREV, 56, false, prevCb);
  lv_obj_align(prev, LV_ALIGN_BOTTOM_RIGHT, -(72 + 56 + 28), rowY);

  s_play = transportBtn(s_content, LV_SYMBOL_PLAY, 72, true, playCb, &s_playLbl);
  lv_obj_align(s_play, LV_ALIGN_BOTTOM_RIGHT, -(56 + 14), rowY);

  lv_obj_t *next = transportBtn(s_content, LV_SYMBOL_NEXT, 56, false, nextCb);
  lv_obj_align(next, LV_ALIGN_BOTTOM_RIGHT, 0, rowY);
}

void uiInit() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(JB_SCREEN_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  buildRail(scr);

  // Content area sits right of the rail, inset by the design's gutters.
  s_content = panel(scr, SCREEN_W - RAIL_W - PAD_X * 2, SCREEN_H, JB_SCREEN_BG, 0);
  lv_obj_align(s_content, LV_ALIGN_TOP_LEFT, RAIL_W + PAD_X, 0);

  buildStatusBar();
  buildNowPlaying();
  buildTransport();

  backlightSet(100);
}

static void fmtTime(char *out, size_t n, uint32_t sec, bool negative) {
  snprintf(out, n, "%s%lu:%02lu", negative ? "-" : "", (unsigned long)(sec / 60),
           (unsigned long)(sec % 60));
}

void uiTick() {
  if (s_provisioning) {
    lv_obj_del(s_provisioning);
    s_provisioning = nullptr;
  }

  // Snapshot under the mutex, render from the copy — never hold the lock across LVGL work.
  PlayerState p;
  if (stateLock()) {
    p = g_player;
    g_player.dirty = false;
    stateUnlock();
  }

  setTextIfChanged(s_room, s_shown.room, p.zoneName.length() ? p.zoneName : String("no room"));
  setTextIfChanged(s_title, s_shown.title,
                   p.title.length() ? p.title : String("Nothing playing"));

  String meta = p.artist;
  if (p.album.length()) { if (meta.length()) meta += "  ·  "; meta += p.album; }
  setTextIfChanged(s_meta, s_shown.meta, meta);

  const bool playing = (p.transport == TransportState::Playing);
  s_wasPlaying = playing;
  if (s_shown.playing != (int)playing) {
    s_shown.playing = (int)playing;
    lv_label_set_text(s_playLbl, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(playing ? JB_ACCENT : JB_TEXT_DIM), 0);
    setTextIfChanged(s_badgeSrc, s_shown.badge, playing ? String("PLAYING") : String(""));
  }

  // Scrubber — only the fill width and the two timecodes, and only when they move.
  const lv_coord_t trackW = lv_obj_get_width(s_track);
  int pct = (p.durationSec > 0) ? (int)((uint64_t)p.positionSec * 100 / p.durationSec) : 0;
  if (pct > 100) pct = 100;
  if (pct != s_shown.pct) {
    s_shown.pct = pct;
    lv_obj_set_width(s_fill, trackW * pct / 100);
  }

  char buf[16];
  if (p.positionSec != s_shown.elapsed) {
    s_shown.elapsed = p.positionSec;
    fmtTime(buf, sizeof(buf), p.positionSec, false);
    lv_label_set_text(s_elapsed, buf);
  }
  const uint32_t remain = (p.durationSec > p.positionSec) ? p.durationSec - p.positionSec : 0;
  if (remain != s_shown.remain) {
    s_shown.remain = remain;
    fmtTime(buf, sizeof(buf), remain, true);
    lv_label_set_text(s_remain, buf);
  }

  // Volume.
  const uint8_t vol = p.volume > 100 ? 100 : p.volume;
  if ((int)vol != s_shown.volPct) {
    s_shown.volPct = vol;
    lv_obj_set_width(s_volFill, 260 * vol / 100);
    snprintf(buf, sizeof(buf), "%u", (unsigned)vol);
    lv_label_set_text(s_volPct, buf);
  }
  const uint8_t icon = (p.muted || vol == 0) ? 0 : (vol < 50 ? 1 : 2);
  if (icon != s_shown.volIcon) {
    s_shown.volIcon = icon;
    lv_label_set_text(s_volIcon, icon == 0 ? LV_SYMBOL_MUTE
                                : (icon == 1 ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_VOLUME_MAX));
  }

  // Album art. albumArtTake() reports only on a CHANGE and hands over the decoded descriptor, so
  // this costs nothing on the vast majority of ticks. The art task owns fetching and decoding —
  // never do either on the UI task.
  const lv_image_dsc_t *dsc = nullptr;
  if (albumArtTake(&dsc)) {
    if (dsc) {
      lv_image_set_src(s_artImg, dsc);
      lv_obj_center(s_artImg);
      lv_obj_remove_flag(s_artImg, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_artPh, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_artImg, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(s_artPh, LV_OBJ_FLAG_HIDDEN);
    }
  }

  lv_timer_handler();
}

void uiProvisioning(const char *apSsid) {
  s_provisioning = panel(lv_screen_active(), SCREEN_W, SCREEN_H, JB_SCREEN_BG, 0);
  lv_obj_t *l = label(s_provisioning, "", &lv_font_montserrat_28, JB_TEXT);
  lv_label_set_text_fmt(l, "Join \"%s\"\non your phone to set up Wi-Fi", apSsid);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(l);

  backlightSet(100);
  lv_timer_handler();   // the UI task is not running yet — flush synchronously
}
