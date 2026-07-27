// UX for sonos-jukebox — the wall-mounted 7" controller. Implements core/unit.h.
//
// SCAFFOLD, not the finished UI. This is enough to prove the unit links, boots, and reflects real
// player state on the glass; the designed screens (Now Playing / Rooms / Radio) come next and are
// fully specified by the /sonos-jukebox-design skill — read that before laying anything out, and
// prefer its tokens (mirrored in ui_scale.h) over ad-hoc values.
//
// House rule (see CLAUDE.md > Architecture): the UI never calls Sonos/SOAP or board pins directly.
// It reads the mutex-guarded g_player, posts work to g_pending, and reaches hardware only through
// core/board.h.
#include <Arduino.h>
#include <lvgl.h>

#include "core/board.h"
#include "core/player_state.h"
#include "core/unit.h"
#include "ui_scale.h"

static lv_obj_t *s_room  = nullptr;
static lv_obj_t *s_title = nullptr;
static lv_obj_t *s_meta  = nullptr;
static lv_obj_t *s_state = nullptr;
static lv_obj_t *s_bar   = nullptr;      // progress fill
static lv_obj_t *s_provisioning = nullptr;

static const char *transportName(TransportState t) {
  switch (t) {
    case TransportState::Playing:       return "PLAYING";
    case TransportState::Paused:        return "PAUSED";
    case TransportState::Stopped:       return "STOPPED";
    case TransportState::Transitioning: return "…";
    default:                            return "—";
  }
}

void uiInit() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(JB_SCREEN_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);

  // Status strip: room name in the accent, transport state at the right. Mirrors the design
  // system's StatusBar component.
  s_room = lv_label_create(scr);
  lv_label_set_text(s_room, "—");
  lv_obj_set_style_text_color(s_room, lv_color_hex(JB_ACCENT), 0);
  lv_obj_set_style_text_font(s_room, &lv_font_montserrat_24, 0);
  lv_obj_align(s_room, LV_ALIGN_TOP_LEFT, SW(4), SH(5));

  s_state = lv_label_create(scr);
  lv_label_set_text(s_state, "—");
  lv_obj_set_style_text_color(s_state, lv_color_hex(JB_TEXT_DIM), 0);
  lv_obj_set_style_text_font(s_state, &lv_font_montserrat_20, 0);
  lv_obj_align(s_state, LV_ALIGN_TOP_RIGHT, -SW(4), SH(5));

  // Now playing.
  s_title = lv_label_create(scr);
  lv_label_set_text(s_title, "Sonos Jukebox");
  lv_obj_set_style_text_color(s_title, lv_color_hex(JB_TEXT), 0);
  lv_obj_set_style_text_font(s_title, &lv_font_montserrat_48, 0);
  lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_title, SW(92));
  lv_obj_align(s_title, LV_ALIGN_LEFT_MID, SW(4), -SH(6));

  s_meta = lv_label_create(scr);
  lv_label_set_text(s_meta, "starting up");
  lv_obj_set_style_text_color(s_meta, lv_color_hex(JB_TEXT_MUTED), 0);
  lv_obj_set_style_text_font(s_meta, &lv_font_montserrat_24, 0);
  lv_label_set_long_mode(s_meta, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_meta, SW(92));
  lv_obj_align(s_meta, LV_ALIGN_LEFT_MID, SW(4), SH(3));

  // Scrubber track + fill.
  lv_obj_t *track = lv_obj_create(scr);
  lv_obj_remove_style_all(track);
  lv_obj_set_size(track, SW(92), 8);
  lv_obj_set_style_bg_color(track, lv_color_hex(JB_SCREEN_ELEV_2), 0);
  lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(track, 4, 0);
  lv_obj_align(track, LV_ALIGN_BOTTOM_LEFT, SW(4), -SH(12));

  s_bar = lv_obj_create(track);
  lv_obj_remove_style_all(s_bar);
  lv_obj_set_size(s_bar, 0, 8);
  lv_obj_set_style_bg_color(s_bar, lv_color_hex(JB_ACCENT), 0);
  lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s_bar, 4, 0);
  lv_obj_align(s_bar, LV_ALIGN_LEFT_MID, 0, 0);

  backlightSet(100);
}

void uiTick() {
  // The provisioning overlay is drawn before the UI task exists; tear it down the moment normal
  // ticking starts (by which point WiFi is provisioned).
  if (s_provisioning) {
    lv_obj_del(s_provisioning);
    s_provisioning = nullptr;
  }

  // Snapshot under the mutex, then render from the copy — never hold the lock across LVGL work.
  PlayerState p;
  if (stateLock()) {
    p = g_player;
    g_player.dirty = false;
    stateUnlock();
  }

  lv_label_set_text(s_room, p.zoneName.length() ? p.zoneName.c_str() : "no room");
  lv_label_set_text(s_state, transportName(p.transport));
  lv_label_set_text(s_title, p.title.length() ? p.title.c_str() : "Nothing playing");

  String meta = p.artist;
  if (p.album.length()) { if (meta.length()) meta += "  ·  "; meta += p.album; }
  lv_label_set_text(s_meta, meta.length() ? meta.c_str() : "");

  int pct = (p.durationSec > 0) ? (int)((uint64_t)p.positionSec * 100 / p.durationSec) : 0;
  if (pct > 100) pct = 100;
  lv_obj_set_width(s_bar, SW(92) * pct / 100);

  lv_timer_handler();
}

// appBoot() calls this right before the captive portal blocks, and the UI task is not running yet
// — so draw and flush synchronously here. Removed on the first uiTick().
void uiProvisioning(const char *apSsid) {
  s_provisioning = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(s_provisioning);
  lv_obj_set_size(s_provisioning, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(s_provisioning, lv_color_hex(JB_SCREEN_BG), 0);
  lv_obj_set_style_bg_opa(s_provisioning, LV_OPA_COVER, 0);

  lv_obj_t *l = lv_label_create(s_provisioning);
  lv_label_set_text_fmt(l, "Join \"%s\"\non your phone to set up Wi-Fi", apSsid);
  lv_obj_set_style_text_color(l, lv_color_hex(JB_TEXT), 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(l);

  backlightSet(100);
  lv_timer_handler();
}
