// LVGL screen state machine. See plan §6.
//   HOME (menu) -> NOW PLAYING / PLAYLISTS / RADIO / ROOMS / SETTINGS
//   Inactivity -> CLOCK screensaver; any input wakes.
#pragma once

enum class Screen { Home, NowPlaying, Playlists, Radio, Rooms, Group, Settings, Clock };

void uiInit();    // build LVGL screens/widgets
void uiTick();    // lv_timer_handler() + route encoder/button/touch input
void uiShow(Screen s);
