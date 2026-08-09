// See art_cache.h.
#include "art_cache.h"

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <TJpg_Decoder.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdio.h>
#include <sys/stat.h>

#include "album_art.h"   // jpegLock/jpegUnlock — TJpgDec is a shared singleton
#include "amazon.h"
#include "board.h"
#include "net/logmirror.h"   // LOG — tees to the TCP mirror where enabled, plain Serial otherwise
#include "heap_watch.h"   // heapwatch::note — attribute the heap low-water

namespace artcache {

struct Slot {
  String   key;
  uint32_t used = 0;         // millis of last get(); 0 = never
  bool     ready = false;
  uint16_t *pix = nullptr;
  lv_image_dsc_t dsc {};
};

static Slot   *s_slots = nullptr;
static int     s_nSlots = 0, s_px = 72;
static uint8_t *s_jpeg = nullptr;                 // download scratch, PSRAM
static const size_t kJpegMax = 48 * 1024;         // a 128 px tile is ~5 KB; this is generous
static QueueHandle_t s_q = nullptr;
static volatile uint32_t s_gen = 1;
static SemaphoreHandle_t s_lock = nullptr;        // guards the slot table

struct Req { char key[24]; char url[300]; };   // favourite art URLs run long

// TJpg writes into whichever slot the worker is filling.
static uint16_t *s_target = nullptr;
static bool tjpgCb(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (!s_target) return false;
  for (int16_t r = 0; r < h; r++) {
    const int16_t dy = y + r;
    if (dy < 0 || dy >= s_px) continue;
    for (int16_t c = 0; c < w; c++) {
      const int16_t dx = x + c;
      if (dx < 0 || dx >= s_px) continue;
      s_target[dy * s_px + dx] = bitmap[r * w + c];
    }
  }
  return true;
}

String keyOf(const String &stationId) {
  const int a = stationId.indexOf("stations/");
  if (a < 0) return "";
  const int s = a + 9;
  const int e = stationId.indexOf('/', s);
  return (e < 0) ? stationId.substring(s) : stationId.substring(s, e);
}

String keyOfUrl(const String &url) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < url.length(); ++i) { h ^= (uint8_t)url[i]; h *= 16777619u; }
  char b[12]; snprintf(b, sizeof b, "u%08x", (unsigned)h);
  return String(b);
}

// Ask the image host for a small version. Every one of these serves originals far larger than a
// 72 px tile — an Amazon cover is up to 6.7 MB and often PNG, which TJpg cannot decode at all — so
// fetching full size would be both slow on a fragile link and useless. Unknown hosts fall through
// unchanged and are simply downloaded as-is.
static String thumbUrl(const String &url, int px) {
  if (url.indexOf("media-amazon.com") >= 0 || url.indexOf("ssl-images-amazon.com") >= 0)
    return amazon::artThumbUrl(url, px > 96 ? 160 : 128);   // also transcodes PNG -> baseline JPEG
  if (url.indexOf("googleusercontent.com") >= 0) {
    const int eq = url.lastIndexOf('=');                    // strip an existing =sNNN / =wNNN-hNNN
    return (eq > 0 ? url.substring(0, eq) : url) + "=s" + String(px * 2);
  }
  if (url.indexOf("i.ytimg.com") >= 0) {
    const int slash = url.lastIndexOf('/');
    if (slash > 0) return url.substring(0, slash) + "/mqdefault.jpg";   // 320x180, ~10 KB
  }
  if (url.indexOf("i.scdn.co/image/") >= 0) {
    // Spotify encodes the size in the id prefix: b273 = 640, e02 = 300, 851 = 64.
    String u = url; u.replace("ab67616d0000b273", "ab67616d00001e02");
    return u;
  }
  return url;
}

static String dir() {
  const char *r = localStorageRoot();
  // NOT under /radio: that whole tree is rmTree'd and rename()d by every crawl (radio_cache.cpp).
  // Living inside it made rmdir() fail, so the swap failed and the crawl never published — and had
  // it "worked", every daily crawl would have thrown away ~1000 thumbnails and re-downloaded them,
  // which is the exact network load the cache exists to avoid.
  return r ? String(r) + "/radioart" : String();
}
static String pathOf(const String &key) { return dir() + "/" + key + ".jpg"; }

// --- worker ---------------------------------------------------------------------------------------

// Read a cached JPEG off the card, or fetch and store it. Returns bytes in s_jpeg, 0 on failure.
static size_t obtain(const Req &r) {
  const String p = pathOf(r.key);
  if (!dir().isEmpty()) {
    FILE *f = fopen(p.c_str(), "rb");
    if (f) {
      const size_t n = fread(s_jpeg, 1, kJpegMax, f);
      fclose(f);
      if (n > 100) return n;          // disk hit: no network, no TLS
    }
  }
  if (!r.url[0]) return 0;

  // HTTPS is mandatory — Amazon 403s plain HTTP on both image hosts. One client, reused across
  // requests, because a fresh TLS handshake per tile would dominate the cost of a browse.
  static WiFiClientSecure *cli = nullptr;
  if (!cli) { cli = new WiFiClientSecure(); cli->setInsecure(); }
  HTTPClient http;
  http.setReuse(true);
  http.setTimeout(12000);
  if (!http.begin(*cli, r.url)) return 0;
  const int code = http.GET();
  if (code != 200) { http.end(); return 0; }
  const int len = http.getSize();
  if (len > (int)kJpegMax) { http.end(); return 0; }
  WiFiClient *st = http.getStreamPtr();
  size_t got = 0;
  const uint32_t deadline = millis() + 12000;
  while (millis() < deadline && got < kJpegMax) {
    const size_t avail = st->available();
    if (!avail) { if (!http.connected() && (len < 0 || got >= (size_t)len)) break; delay(5); continue; }
    got += st->readBytes(s_jpeg + got, min(avail, kJpegMax - got));
    heapwatch::note("artcache.fetch");
    if (len > 0 && got >= (size_t)len) break;
  }
  http.end();
  if (got < 100) return 0;

  if (!dir().isEmpty()) {                 // persist so this is the last time we pay for it
    mkdir(dir().c_str(), 0777);
    FILE *f = fopen(p.c_str(), "wb");
    if (f) {
      // The 4 KB write rule (plans/08) applies to every writer on this board, not just the crawler.
      size_t left = got; const uint8_t *q = s_jpeg;
      while (left) { const size_t n = left > 4096 ? 4096 : left;
                     if (fwrite(q, 1, n, f) != n) break; q += n; left -= n; }
      fclose(f);
    }
  }
  return got;
}

// Least-recently-used slot that is not the one we are about to draw from. Slots never in use are
// preferred, so a cold cache fills before anything is evicted.
static int victim() {
  int best = 0; uint32_t oldest = UINT32_MAX;
  for (int i = 0; i < s_nSlots; i++) {
    if (!s_slots[i].ready) return i;
    if (s_slots[i].used < oldest) { oldest = s_slots[i].used; best = i; }
  }
  return best;
}

static void worker(void *) {
  Req r;
  for (;;) {
    if (xQueueReceive(s_q, &r, portMAX_DELAY) != pdTRUE) continue;

    // Someone may have filled it while this sat in the queue.
    bool have = false;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
      for (int i = 0; i < s_nSlots; i++)
        if (s_slots[i].ready && s_slots[i].key == r.key) { have = true; break; }
      xSemaphoreGive(s_lock);
    }
    if (have) continue;

    const size_t n = obtain(r);
    if (!n) continue;

    if (!jpegLock()) continue;            // shared with album art — see album_art.h
    uint16_t w = 0, h = 0;
    if (TJpgDec.getJpgSize(&w, &h, s_jpeg, n) != JDR_OK || !w || !h) { jpegUnlock(); continue; }
    uint8_t scale = 1;
    while ((max(w, h) / scale) > s_px && scale < 8) scale <<= 1;

    int slot;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) { jpegUnlock(); continue; }
    slot = victim();
    s_slots[slot].ready = false;          // taken out of service while it is rewritten
    s_slots[slot].key   = r.key;
    xSemaphoreGive(s_lock);

    s_target = s_slots[slot].pix;
    memset(s_target, 0, (size_t)s_px * s_px * 2);
    TJpgDec.setJpgScale(scale);
    TJpgDec.setSwapBytes(false);
    TJpgDec.setCallback(tjpgCb);
    const JRESULT jr = TJpgDec.drawJpg(0, 0, s_jpeg, n);
    s_target = nullptr;
    jpegUnlock();

    if (jr != JDR_OK) {
      LOG.printf("[artc  ] decode failed jr=%d for %s (%u B)\n", (int)jr, r.key, (unsigned)n);
      continue;                            // slot stays not-ready and will be reused
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
      s_slots[slot].ready = true;
      s_slots[slot].used  = millis();
      xSemaphoreGive(s_lock);
    }
    s_gen++;
  }
}

// --- api -------------------------------------------------------------------------------------------

bool init(int tilePx, int slots) {
  if (s_slots) return true;
  s_px = tilePx; s_nSlots = slots;
  s_slots = new Slot[slots];
  const size_t px = (size_t)s_px * s_px * 2;
  for (int i = 0; i < slots; i++) {
    s_slots[i].pix = (uint16_t *)heap_caps_malloc(px, MALLOC_CAP_SPIRAM);
    if (!s_slots[i].pix) { LOG.println("[artc  ] PSRAM alloc failed"); return false; }
    lv_image_dsc_t &d = s_slots[i].dsc;
    memset(&d, 0, sizeof d);
    d.header.magic  = LV_IMAGE_HEADER_MAGIC;
    d.header.cf     = LV_COLOR_FORMAT_RGB565;
    d.header.w      = s_px;
    d.header.h      = s_px;
    d.header.stride = s_px * 2;
    d.data          = (const uint8_t *)s_slots[i].pix;
    d.data_size     = px;
  }
  s_jpeg = (uint8_t *)heap_caps_malloc(kJpegMax, MALLOC_CAP_SPIRAM);
  s_lock = xSemaphoreCreateMutex();
  s_q    = xQueueCreate(24, sizeof(Req));
  if (!s_jpeg || !s_lock || !s_q) return false;
  // Core 0 with the network, low priority: fetching tiles must never compete with rendering.
  xTaskCreatePinnedToCore(worker, "artcache", 6144, nullptr, 1, nullptr, 0);
  LOG.printf("[artc  ] %d slots x %dx%d = %u KB PSRAM\n", slots, s_px, s_px,
                (unsigned)(px * slots / 1024));
  return true;
}

uint32_t generation() { return s_gen; }

const lv_image_dsc_t *get(const String &stationKey, const String &artUrl) {
  if (!s_slots || stationKey.isEmpty()) return nullptr;
  const lv_image_dsc_t *hit = nullptr;
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
    for (int i = 0; i < s_nSlots; i++) {
      if (s_slots[i].ready && s_slots[i].key == stationKey) {
        s_slots[i].used = millis();       // LRU touch happens on the read, not on a separate call
        hit = &s_slots[i].dsc;
        break;
      }
    }
    xSemaphoreGive(s_lock);
  }
  if (hit || artUrl.isEmpty()) return hit;

  // Miss: queue it. The queue is short and drops when full rather than blocking the UI task — a
  // fast flick past fifty rows should not enqueue fifty fetches, and the rows still on screen when
  // it settles will simply ask again on the next pass.
  Req r {};
  strncpy(r.key, stationKey.c_str(), sizeof r.key - 1);
  const String small = thumbUrl(artUrl, s_px);
  strncpy(r.url, small.c_str(), sizeof r.url - 1);
  xQueueSend(s_q, &r, 0);
  return nullptr;
}

}  // namespace artcache
