// sleep_machine unit — STUB UX for the rectangular 2.8" ES3C28P (240x320, touch-first).
//
// This is a placeholder that satisfies the unit contract (core/unit.h) and shows the live
// now-playing title/artist, proving the shared core drives a second unit. The real
// sleep-machine UX (bedside clock home, alarms / wake-to-music, sleep timer, ambient
// sounds off the microSD) is deferred — design and build it here. Reuses the shared core
// as-is: g_player / g_pending (core/player_state.h), library::, sonos::, core/album_art.h.
#include "core/unit.h"
#include "core/player_state.h"
#include "ui_scale.h"
#include <lvgl.h>

static lv_obj_t *s_title;   // unit name banner
static lv_obj_t *s_track;   // live now-playing text

void uiInit() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  s_title = lv_label_create(scr);
  lv_obj_set_style_text_color(s_title, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_title, &lv_font_montserrat_20, 0);
  lv_label_set_text(s_title, "sonos-sleep-machine");
  lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, SH(8));

  s_track = lv_label_create(scr);
  lv_obj_set_style_text_color(s_track, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_text_font(s_track, &lv_font_montserrat_24, 0);
  lv_label_set_long_mode(s_track, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_track, SW(90));
  lv_obj_set_style_text_align(s_track, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(s_track, "(no track)");
  lv_obj_center(s_track);
}

void uiTick() {
  // Refresh the now-playing text when the poll task marks state dirty.
  if (stateLock()) {
    if (g_player.dirty) {
      String t = g_player.title.length()
                     ? g_player.title + "\n" + g_player.artist
                     : String("(no track)");
      lv_label_set_text(s_track, t.c_str());
      g_player.dirty = false;
    }
    stateUnlock();
  }
  lv_timer_handler();
}
