#include "screens.h"
#include "ui_scale.h"
#include "../hw/encoder.h"
#include "../hw/pcf8574.h"

// TODO Phase 2: NOW PLAYING — art, title, artist, progress arc.
//   twist = volume +/-  ->  setVolume
//   short push = play/pause
//   touch L/R or long-twist = prev/next
// TODO Phase 3: PLAYLISTS (Browse SQ:) and RADIO (Browse R:0/FV:2) scrollable lists.
// TODO Phase 4: ROOMS zone switcher, SETTINGS, CLOCK screensaver on inactivity.

void uiInit() {
  // TODO: create screens; default to NOW PLAYING when active, else HOME.
}

void uiTick() {
  // TODO: lv_timer_handler();
  // int32_t d = encoderDelta();  -> volume or list scroll depending on screen
  // if (knobPressed())           -> enter / play-pause depending on screen
}

void uiShow(Screen) {
  // TODO
}
