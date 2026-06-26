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

// A Stream sink that appends into a fixed buffer. Lets HTTPClient::writeToStream() do the
// chunked-transfer de-framing for us (reading the raw stream pointer does NOT de-chunk).
class BufSink : public Stream {
 public:
  BufSink(uint8_t *b, size_t cap) : _b(b), _cap(cap) {}
  size_t len = 0;
  size_t write(uint8_t c) override { if (len < _cap) { _b[len++] = c; return 1; } return 0; }
  size_t write(const uint8_t *d, size_t n) override {
    size_t t = (len + n <= _cap) ? n : (_cap - len);
    memcpy(_b + len, d, t); len += t; return t;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
 private:
  uint8_t *_b; size_t _cap;
};

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
  int code = http.GET();
  if (code != 200) { Serial.printf("[art] HTTP %d\n", code); http.end(); return false; }
  // writeToStream() de-chunks the body (the raw stream pointer would include chunk framing).
  BufSink sink(s_jpeg, JPEG_MAX);
  http.writeToStream(&sink);
  size_t got = sink.len;
  http.end();
  if (got < 100) { Serial.printf("[art] short read (%u bytes)\n", (unsigned)got); return false; }

  // Size + pick a power-of-2 downscale so the long edge fits ART_MAX.
  uint16_t w = 0, h = 0;
  if (TJpgDec.getJpgSize(&w, &h, s_jpeg, got) != JDR_OK || !w || !h) {
    Serial.printf("[art] getJpgSize failed (%u bytes)\n", (unsigned)got);
    return false;
  }
  uint8_t scale = 1;
  while ((max(w, h) / scale) > ART_MAX && scale < 8) scale <<= 1;
  TJpgDec.setJpgScale(scale);
  s_decW = min((int)(w / scale), ART_MAX);
  s_decH = min((int)(h / scale), ART_MAX);

  memset(s_buf[s_front ^ 1], 0, ART_MAX * ART_MAX * 2);
  JRESULT jr = TJpgDec.drawJpg(0, 0, s_jpeg, got);
  if (jr != JDR_OK) {
    Serial.printf("[art] drawJpg failed jr=%d  hdr=%02X%02X  %ux%u  %u bytes\n",
                  (int)jr, s_jpeg[0], s_jpeg[1], w, h, (unsigned)got);
    return false;
  }

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
