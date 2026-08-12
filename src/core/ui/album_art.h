// Album-art pipeline: fetch the speaker's art JPEG (plain HTTP), software-decode +
// downscale with TJpg_Decoder into a cached RGB565 buffer, expose it as an LVGL image.
// Decode happens on the art_task (never the UI task), once per track change. Plan §3/§7.
#pragma once

#include <Arduino.h>
#include <lvgl.h>

// TJpgDec is a GLOBAL SINGLETON: one callback pointer, one scale factor, shared by every caller.
// Anything else in the firmware that decodes a JPEG must hold this for the duration, or two
// decoders running on different tasks will overwrite each other's callback and scale mid-frame.
bool jpegLock(uint32_t timeoutMs = 8000);
void jpegUnlock();

bool albumArtInit();                 // allocate PSRAM buffers; call once after PSRAM is up
bool albumArtFetch(const String &url);  // GET + decode into the back buffer, then swap
void albumArtClear();                // mark "no art" (e.g. radio with no image)

// UI side: returns true once when the art changed since the last call. *dscOut is the
// image descriptor to display, or nullptr when art was cleared.
bool albumArtTake(const lv_image_dsc_t **dscOut);

// What the pipeline has actually done since boot. `clears` is the interesting one: the UI hides
// the cover ONLY when albumArtTake() hands back nullptr, which only happens after albumArtClear().
// If the artwork is visibly flickering, either this is climbing (something is clearing it) or it
// is not (and the churn is fetch/decode, not clearing).
//
// `progressives` counts covers TJpgDec refused that the libjpeg fallback then decoded (issue #16).
// It is the one number that says the fallback is earning its ~78 KB of flash: if it stays 0 on a
// unit that plays Amazon Prime Stations, either the content changed or the fallback is not wired in.
struct AlbumArtDiag { uint32_t fetches, failures, clears, decodeFails, progressives; };
void albumArtDiag(AlbumArtDiag &out);
