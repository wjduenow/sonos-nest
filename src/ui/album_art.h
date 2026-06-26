// Album-art pipeline: fetch the speaker's art JPEG (plain HTTP), software-decode +
// downscale with TJpg_Decoder into a cached RGB565 buffer, expose it as an LVGL image.
// Decode happens on the art_task (never the UI task), once per track change. Plan §3/§7.
#pragma once

#include <Arduino.h>
#include <lvgl.h>

bool albumArtInit();                 // allocate PSRAM buffers; call once after PSRAM is up
bool albumArtFetch(const String &url);  // GET + decode into the back buffer, then swap
void albumArtClear();                // mark "no art" (e.g. radio with no image)

// UI side: returns true once when the art changed since the last call. *dscOut is the
// image descriptor to display, or nullptr when art was cleared.
bool albumArtTake(const lv_image_dsc_t **dscOut);
