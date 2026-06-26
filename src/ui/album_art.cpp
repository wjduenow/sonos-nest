#include "album_art.h"
#include "../player_state.h"
#include <TJpg_Decoder.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

// Decoded art is capped to ART_MAX px on the long edge (power-of-2 downscale via TJpgDec).
static const int    ART_MAX  = 180;
static const size_t JPEG_MAX = 100 * 1024;

static uint8_t  *s_jpeg = nullptr;          // raw JPEG download buffer (PSRAM)
static uint16_t *s_buf[2] = {nullptr, nullptr};  // double-buffered RGB565 (PSRAM)
static int       s_front = 0;               // index currently shown
static int       s_decW = 0, s_decH = 0;    // dimensions being decoded into the back buffer

static lv_image_dsc_t s_dsc;
static volatile bool s_changed = false;     // new art (or clear) pending for the UI
static volatile bool s_hasArt  = false;

// TJpgDec block callback: copy one decoded MCU block into the back buffer.
static bool tjpgCb(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  uint16_t *dst = s_buf[s_front ^ 1];
  for (int row = 0; row < h; ++row) {
    int dy = y + row;
    if (dy >= s_decH) break;
    for (int col = 0; col < w; ++col) {
      int dx = x + col;
      if (dx >= s_decW) continue;
      dst[dy * s_decW + dx] = bitmap[row * w + col];
    }
  }
  return true;
}

bool albumArtInit() {
  s_jpeg   = (uint8_t *)heap_caps_malloc(JPEG_MAX, MALLOC_CAP_SPIRAM);
  s_buf[0] = (uint16_t *)heap_caps_malloc(ART_MAX * ART_MAX * 2, MALLOC_CAP_SPIRAM);
  s_buf[1] = (uint16_t *)heap_caps_malloc(ART_MAX * ART_MAX * 2, MALLOC_CAP_SPIRAM);
  if (!s_jpeg || !s_buf[0] || !s_buf[1]) return false;

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(false);   // RGB565 native order (matches the panel); flip if colors invert
  TJpgDec.setCallback(tjpgCb);

  memset(&s_dsc, 0, sizeof(s_dsc));
  s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  s_dsc.header.cf    = LV_COLOR_FORMAT_RGB565;
  return true;
}

bool albumArtFetch(const String &url) {
  if (!s_jpeg) return false;

  // Download the JPEG (plain HTTP — Sonos serves art off the speaker itself).
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) return false;
  if (http.GET() != 200) { http.end(); return false; }
  int len = http.getSize();
  WiFiClient *st = http.getStreamPtr();
  size_t got = 0;
  uint32_t deadline = millis() + 5000;
  while (http.connected() && got < JPEG_MAX && (int32_t)(deadline - millis()) > 0) {
    size_t avail = st->available();
    if (avail) {
      int r = st->readBytes(s_jpeg + got, min(avail, JPEG_MAX - got));
      if (r > 0) got += r;
    } else {
      delay(1);
    }
    if (len >= 0 && got >= (size_t)len) break;
  }
  http.end();
  if (got < 100) return false;

  // Size + pick a power-of-2 downscale so the long edge fits ART_MAX.
  uint16_t w = 0, h = 0;
  if (TJpgDec.getJpgSize(&w, &h, s_jpeg, got) != JDR_OK || !w || !h) return false;
  uint8_t scale = 1;
  while ((max(w, h) / scale) > ART_MAX && scale < 8) scale <<= 1;
  TJpgDec.setJpgScale(scale);
  s_decW = min((int)(w / scale), ART_MAX);
  s_decH = min((int)(h / scale), ART_MAX);

  memset(s_buf[s_front ^ 1], 0, ART_MAX * ART_MAX * 2);
  if (TJpgDec.drawJpg(0, 0, s_jpeg, got) != JDR_OK) return false;

  // Publish: swap buffers and update the descriptor under the lock.
  if (stateLock()) {
    s_front = s_front ^ 1;
    s_dsc.data          = (const uint8_t *)s_buf[s_front];
    s_dsc.data_size     = (uint32_t)s_decW * s_decH * 2;
    s_dsc.header.w      = s_decW;
    s_dsc.header.h      = s_decH;
    s_dsc.header.stride = s_decW * 2;
    s_hasArt  = true;
    s_changed = true;
    stateUnlock();
  }
  return true;
}

void albumArtClear() {
  if (stateLock()) { s_hasArt = false; s_changed = true; stateUnlock(); }
}

bool albumArtTake(const lv_image_dsc_t **dscOut) {
  bool changed = false;
  if (stateLock()) {
    if (s_changed) {
      changed = true;
      s_changed = false;
      *dscOut = s_hasArt ? &s_dsc : nullptr;
    }
    stateUnlock();
  }
  return changed;
}
