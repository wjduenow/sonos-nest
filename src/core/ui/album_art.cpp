#include "album_art.h"
#include "core/player_state.h"
#include <TJpg_Decoder.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include "core/net/logmirror.h"   // LOG — tees to the TCP mirror where enabled, plain Serial otherwise
#include "core/heap_watch.h"   // heapwatch::note — attribute the heap low-water (heap_watch.h)

// Decoded art is capped to ART_MAX px on the long edge (power-of-2 downscale via TJpgDec).
// Per-unit, because it is a function of panel size: 180 suits the nest's 480x480 and the
// sleep-machine's 320x240, while the jukebox's 7" 1024x600 uses 320. Costs ART_MAX^2 * 2 * 2 bytes
// of PSRAM (double-buffered): 130 KB at 180, 410 KB at 320 — irrelevant against ~7 MB free on the
// S3 boards and ~31 MB on the P4. Override per env with -DART_MAX_PX=<n>.
//
// *** THIS IS A CEILING THE DECODER UNDERSHOOTS, NOT A TARGET. *** TJpgDec scales only by powers
// of two, so the decoded size is source/(1,2,4,8) — the largest that fits under the cap. Sonos
// serves 640 px covers, so a cap of 280 rejects /2 (320 > 280) and yields **160 px**: a cap 100 px
// higher than the tile still produced art at well under half of it. Choosing a cap therefore means
// asking "what does source/2^n land on", not "how big do I want it". 320 is chosen for exactly
// that reason on the jukebox. Symptom to recognise: art that is soft at every size, on a device
// with megabytes of spare PSRAM.
#ifndef ART_MAX_PX
#define ART_MAX_PX 180
#endif
static const int    ART_MAX  = ART_MAX_PX;
// Raw JPEG download cap. 220 KB was too tight: a real Sonos cover measured 228,077 bytes, i.e.
// it overran the buffer and was silently truncated (see BufSink::dropped below). Art comes from
// the streaming service via the speaker, so the ceiling isn't ours to control — leave generous
// headroom. This lives in PSRAM, which is nowhere near the constraint on any unit (~7 MB free on
// the ESP32-S3 boards, ~31 MB on the P4); internal SRAM, which IS tight, is unaffected.
static const size_t JPEG_MAX = 512 * 1024;

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
  size_t dropped = 0;   // bytes that did not fit — see albumArtFetch(), which must not ignore this
  size_t write(uint8_t c) override {
    if (len < _cap) { _b[len++] = c; return 1; }
    dropped++; return 0;
  }
  size_t write(const uint8_t *d, size_t n) override {
    size_t t = (len + n <= _cap) ? n : (_cap - len);
    memcpy(_b + len, d, t); len += t;
    dropped += n - t;
    return t;
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

static SemaphoreHandle_t s_jpegMutex = nullptr;

bool jpegLock(uint32_t timeoutMs) {
  if (!s_jpegMutex) s_jpegMutex = xSemaphoreCreateMutex();
  return s_jpegMutex && xSemaphoreTake(s_jpegMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}
void jpegUnlock() { if (s_jpegMutex) xSemaphoreGive(s_jpegMutex); }

bool albumArtInit() {
  if (!s_jpegMutex) s_jpegMutex = xSemaphoreCreateMutex();
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

// Pipeline counters — see AlbumArtDiag in album_art.h. Plain uint32 stores written by artTask and
// read by the HTTP task; a torn read skews one diagnostic number and nothing else.
static uint32_t s_nFetch = 0, s_nFail = 0, s_nClear = 0, s_nDecodeFail = 0;
void albumArtDiag(AlbumArtDiag &out) {
  out.fetches = s_nFetch; out.failures = s_nFail; out.clears = s_nClear; out.decodeFails = s_nDecodeFail;
}

bool albumArtFetch(const String &url) {
  ++s_nFetch;
  if (!s_jpeg) { ++s_nFail; return false; }

  // Download the JPEG (plain HTTP — Sonos serves art off the speaker itself).
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) { ++s_nFail; return false; }
  int code = http.GET();
  if (code != 200) { LOG.printf("[art] HTTP %d\n", code); http.end(); ++s_nFail; return false; }
  // writeToStream() de-chunks the body (the raw stream pointer would include chunk framing).
  BufSink sink(s_jpeg, JPEG_MAX);
  http.writeToStream(&sink);
  size_t got = sink.len;
  heapwatch::note("art.fetch");
  size_t dropped = sink.dropped;
  http.end();
  if (got < 100) { LOG.printf("[art] short read (%u bytes)\n", (unsigned)got); ++s_nFail; return false; }
  // A cover larger than JPEG_MAX used to be truncated silently: TJpg then failed or produced a
  // garbled image, with nothing in the log pointing at the buffer. Refuse it loudly instead —
  // no art beats wrong art, and the message says exactly what to raise.
  if (dropped) {
    LOG.printf("[art] TRUNCATED: %u bytes did not fit (JPEG_MAX=%u). Raise JPEG_MAX.\n",
                  (unsigned)dropped, (unsigned)JPEG_MAX);
    return false;
  }

  // Size + pick a power-of-2 downscale so the long edge fits ART_MAX.
  if (!jpegLock()) { LOG.println("[art] decoder busy"); ++s_nFail; return false; }
  uint16_t w = 0, h = 0;
  if (TJpgDec.getJpgSize(&w, &h, s_jpeg, got) != JDR_OK || !w || !h) {
    jpegUnlock();
    LOG.printf("[art] getJpgSize failed (%u bytes)\n", (unsigned)got);
    return false;
  }
  uint8_t scale = 1;
  while ((max(w, h) / scale) > ART_MAX && scale < 8) scale <<= 1;
  TJpgDec.setJpgScale(scale);
  s_decW = min((int)(w / scale), ART_MAX);
  s_decH = min((int)(h / scale), ART_MAX);

  memset(s_buf[s_front ^ 1], 0, ART_MAX * ART_MAX * 2);
  TJpgDec.setCallback(tjpgCb);      // re-assert: another decoder may have pointed it elsewhere
  JRESULT jr = TJpgDec.drawJpg(0, 0, s_jpeg, got);
  jpegUnlock();
  if (jr != JDR_OK) {
    LOG.printf("[art] drawJpg failed jr=%d  hdr=%02X%02X  %ux%u  %u bytes\n",
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
  ++s_nClear;
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
