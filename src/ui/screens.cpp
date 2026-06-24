#include "screens.h"
#include "ui_scale.h"
#include "../hw/encoder.h"
#include "../hw/pcf8574.h"
#include <lvgl.h>

// TODO Phase 2: NOW PLAYING — art, title, artist, progress arc.
//   twist = volume +/-  ->  setVolume
//   short push = play/pause
//   touch L/R or long-twist = prev/next
// TODO Phase 3: PLAYLISTS (Browse SQ:) and RADIO (Browse R:0/FV:2) scrollable lists.
// TODO Phase 4: ROOMS zone switcher, SETTINGS, CLOCK screensaver on inactivity.

// Phase-0 placeholder screen: an animated spinner + label. Its constant redraw is the
// stress test for the "flicker-free redraw while WiFi is active" exit gate (plan §2).
static lv_obj_t *s_value;

void uiInit() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  lv_obj_t *spinner = lv_spinner_create(scr);
  lv_obj_set_size(spinner, SW(40), SH(40));
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -SH(8));
  lv_spinner_set_anim_params(spinner, 1000, 200);

  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "sonos-nest");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, SH(14));

  s_value = lv_label_create(scr);
  lv_obj_set_style_text_color(s_value, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_label_set_text(s_value, "twist me");
  lv_obj_align(s_value, LV_ALIGN_CENTER, 0, SH(22));
}

void uiTick() {
  // Reflect encoder/button input so Phase-0 confirms the full input->UI path live.
  static int32_t accum = 0;
  int32_t d = encoderDelta();
  if (d != 0) {
    accum += d;
    lv_label_set_text_fmt(s_value, "enc: %ld", (long)accum);
  }
  if (knobPressed()) {
    lv_label_set_text(s_value, "PRESS");
  }
  lv_timer_handler();
}

void uiShow(Screen) {
  // TODO Phase 2+: swap the active screen.
}
