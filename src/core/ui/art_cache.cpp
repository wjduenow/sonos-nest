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
#include <string.h>   // strncmp/strncpy for the undecodable-key list
#include <sys/stat.h>

#include "album_art.h"   // jpegLock/jpegUnlock — TJpgDec is a shared singleton
#include "jpeg_decode.h" // progressive-JPEG fallback for tiles TJpgDec refuses
#include "core/amazon.h"
#include "core/board.h"
#include "core/net/logmirror.h"   // LOG — tees to the TCP mirror where enabled, plain Serial otherwise
#include "core/heap_watch.h"   // heapwatch::note — attribute the heap low-water

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

// --- Keys whose IMAGE cannot be decoded ------------------------------------------------------
// Without this the cache livelocks. A miss enqueues a fetch; the worker downloads, fails to
// decode, and `continue`s leaving the slot not-ready — and BOTH dedupe checks (get()'s scan and
// the worker's "did someone fill it while this queued") test `ready`, so neither sees the
// attempt. The row is still on screen, so the next UI pass misses again, enqueues again, and the
// worker re-downloads the same bytes to fail identically. Measured on 2026-08-27: ~15 cycles in
// 10 s on one station, internal heap 110 KB -> 51 KB (min 25.5 KB), and favcache had to skip its
// refresh — "[favs] refresh deferred, only 54700 B heap free". CLAUDE.md records where that ends:
// LWIP cannot get socket buffers and the symptom is Sonos "connection refused". It also thrashes
// an LRU slot per attempt, evicting artwork that HAD decoded.
//
// ONLY DECODE FAILURES GO IN HERE, and the distinction is the whole design. A JPEG that TJpg
// rejects will be rejected identically forever, so retrying can only burn heap and bandwidth.
// A failed FETCH is not like that — a blip on this board's famously fragile link is transient, and
// blacklisting on it would blank a tile permanently over one bad moment. Fetch failures therefore
// still retry; if they ever storm the same way, that wants its own backoff, not this list.
//
// Small and wrapping on purpose: ~1055 stations, of which a handful have unusable art. Wrapping
// means a long-ago bad key is eventually retried, so the cache self-heals if the host fixes the
// image, without ever growing.
static const int kMaxBad = 16;
static char s_bad[kMaxBad][sizeof(Req::key)] = {};
static int  s_badNext = 0;

// Both callers already hold s_lock.
static bool isBadLocked(const char *key) {
  for (int i = 0; i < kMaxBad; i++)
    if (s_bad[i][0] && strncmp(s_bad[i], key, sizeof s_bad[0]) == 0) return true;
  return false;
}

static String pathOf(const String &key);   // defined below, next to dir()

static void markBad(const char *key) {
  if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return;
  if (!isBadLocked(key)) {
    strncpy(s_bad[s_badNext], key, sizeof s_bad[0] - 1);
    s_bad[s_badNext][sizeof s_bad[0] - 1] = 0;
    s_badNext = (s_badNext + 1) % kMaxBad;
  }
  xSemaphoreGive(s_lock);

  // Drop the SD copy too. obtain() caches every download, INCLUDING a truncated one, and prefers
  // the disk on the next miss — so without this a short read is cached permanently and replays the
  // same undecodable bytes on every boot, blanking that tile forever. Deleting it costs nothing
  // (the blacklist stops any retry this session) and makes the failure self-healing: the next boot
  // fetches it fresh, which is exactly right if the cause was a truncated transfer rather than an
  // image TJpg genuinely cannot read.
  const String p = pathOf(String(key));
  if (!p.isEmpty()) remove(p.c_str());
}

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
// The mosaic path segment for a given tile size, kept out of thumbUrl() so the string is not
// built twice.
static const char *tile640(int px) { return px <= 96 ? "/60/" : "/300/"; }

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
    // Spotify encodes the rendition in the id prefix, and there are TWO families. Album and track
    // covers: b273 = 640 (52 KB), 1e02 = 300 (18 KB), 4851 = 64 (1.6 KB). ARTIST portraits are a
    // different prefix entirely: e5eb = 640 (134 KB), 5174 = 320 (46 KB), f178 = 160 (15 KB).
    // Sizes measured against live URLs 2026-09-06.
    //
    // Only the album family was rewritten here at first, which silently lost the art on every
    // artist AND every Spotify STATION — artist radio carries the artist portrait — because 134 KB
    // is over kJpegMax and the fetch is dropped before anything is decoded.
    //
    // TILES TAKE THE SMALLEST RENDITION THAT EXISTS. A 72 px tile fed by a 300 px cover spends
    // 18 KB and a TLS round trip per row to throw away 94% of the pixels, and a list is 50 rows
    // over the ESP-Hosted link — which is what "art loads very slowly" was. 64 px upscaled to 72
    // is marginally soft; 11x less data per row is not marginal. Now Playing and the screensaver
    // are unaffected and deliberately so: album_art.cpp never comes through here, it decodes
    // whatever the speaker reports up to ART_MAX_PX.
    const bool tile = (px <= 96);
    String u = url;
    u.replace("ab67616d0000b273", tile ? "ab67616d00004851" : "ab67616d00001e02");  // album 640 ->
    u.replace("ab67616d00001e02", tile ? "ab67616d00004851" : "ab67616d00001e02");  // ...or 300 ->
    u.replace("ab6761610000e5eb", tile ? "ab6761610000f178" : "ab67616100005174");  // artist/radio
    return u;
  }
  if (url.indexOf("mosaic.scdn.co/") >= 0) {
    // Playlist mosaics put the size in a path segment: 640 = 76 KB, 300 = 22 KB, 160 = 7 KB,
    // 60 = 2 KB. Matched on the host rather than on "/640/" so a mosaic served at another size is
    // still normalised. Note the path also CONTAINS ab67616d… cover ids — hence a separate branch,
    // because the album rewrite above would corrupt them.
    String u = url; u.replace("/640/", tile640(px));
    return u;
  }
  // pickasso.spotifycdn.com and seed-mix-image.spotifycdn.com (the other playlist art hosts) have
  // no size knob and already answer at 15-45 KB, so they pass through under the cap.
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

    // Someone may have filled it while this sat in the queue — or it may have been proved
    // undecodable since it was enqueued, which get() cannot have known when it queued it.
    bool have = false;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
      for (int i = 0; i < s_nSlots; i++)
        if (s_slots[i].ready && s_slots[i].key == r.key) { have = true; break; }
      if (!have) have = isBadLocked(r.key);
      xSemaphoreGive(s_lock);
    }
    if (have) continue;

    // PACING, for the same reason radio_cache paces its crawl: back-to-back requests are the
    // sustained-load profile that kills this board's ESP-Hosted link (plans/07), and a screen of
    // rows enqueues a whole screen of fetches at once. 120 ms is invisible on a list that scrolls
    // at human speed and turns a burst into a trickle. It does not CURE the link fault — nothing
    // here does — it stops us provoking it.
    vTaskDelay(pdMS_TO_TICKS(120));

    const size_t n = obtain(r);
    if (!n) continue;

    // Attribute the low-water while the 48 KB scratch buffer is still held — heap_watch.h is
    // explicit that a note AFTER the allocation, while it is live, is the useful one. artcache was
    // untagged, which is why heapLow blamed `poll` at 54 KB during the retry storm while heapMin
    // was 25 KB: exactly the "lower than any recorded tag" gap CLAUDE.md says to treat as the clue.
    heapwatch::note("artcache");

    if (!jpegLock()) continue;            // shared with album art — TRANSIENT, so no blacklist
    uint16_t w = 0, h = 0;
    // A tile whose header TJpgDec will not read is not necessarily a broken tile — it may be
    // progressive (issue #16). Amazon's resizer has produced baseline for every `_SL128_` sampled
    // so far, so this is expected to be rare here, but the whole point of thumbUrl() is that
    // UNKNOWN hosts fall through unresized, and those serve whatever they like.
    const bool tjpgOk = (TJpgDec.getJpgSize(&w, &h, s_jpeg, n) == JDR_OK) && w && h;
    uint8_t scale = 1;
    if (tjpgOk) while ((max(w, h) / scale) > s_px && scale < 8) scale <<= 1;

    int slot;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) { jpegUnlock(); continue; }
    slot = victim();
    s_slots[slot].ready = false;          // taken out of service while it is rewritten
    s_slots[slot].key   = r.key;
    xSemaphoreGive(s_lock);

    s_target = s_slots[slot].pix;
    memset(s_target, 0, (size_t)s_px * s_px * 2);
    JRESULT jr = JDR_OK;
    bool ok;
    if (tjpgOk) {
      TJpgDec.setJpgScale(scale);
      TJpgDec.setSwapBytes(false);
      TJpgDec.setCallback(tjpgCb);
      jr = TJpgDec.drawJpg(0, 0, s_jpeg, n);
      ok = (jr == JDR_OK);
    } else {
      // Tiles are square slots that the image is drawn into at 0,0, so the fallback writes at the
      // slot stride rather than packed — the memset above leaves the remainder black, exactly as
      // the TJpg path does for a non-square image.
      int dw = 0, dh = 0;
      ok = jpegDecodeRgb565(s_jpeg, n, s_target, s_px, s_px, s_px, &dw, &dh);
    }
    s_target = nullptr;
    jpegUnlock();

    if (!ok) {
      LOG.printf("[artc  ] decode failed jr=%d for %s (%u B)%s — not retrying\n", (int)jr, r.key,
                 (unsigned)n, jpegIsProgressive(s_jpeg, n) ? " [progressive]" : "");
      // Blacklisted only HERE, after the libjpeg fallback has also refused the bytes -- at that
      // point the result really is deterministic. Doing it at the TJpgDec header failure instead
      // (which is what this did before the fallback existed) would condemn every progressive tile
      // the fallback is here to rescue.
      markBad(r.key);
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
  bool bad = false;
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
    for (int i = 0; i < s_nSlots; i++) {
      if (s_slots[i].ready && s_slots[i].key == stationKey) {
        s_slots[i].used = millis();       // LRU touch happens on the read, not on a separate call
        hit = &s_slots[i].dsc;
        break;
      }
    }
    if (!hit) bad = isBadLocked(stationKey.c_str());
    xSemaphoreGive(s_lock);
  }
  // THE ENQUEUE IS THE THING THAT HAS TO STOP. This runs from the UI task on every pass over a
  // visible row, so a key that can never decode would otherwise re-request forever no matter what
  // the worker does. Returning nullptr here leaves the tile blank, which is what it was going to
  // be anyway — only now it costs one comparison instead of a download and a failed decode.
  if (bad) return nullptr;
  if (hit || artUrl.isEmpty()) return hit;

  // Miss: queue it. The queue is short and drops when full rather than blocking the UI task — a
  // fast flick past fifty rows should not enqueue fifty fetches, and the rows still on screen when
  // it settles will simply ask again on the next pass.
  const String small = thumbUrl(artUrl, s_px);
  // A PNG is a guaranteed decode failure — the decoders here are JPEG-only — so fetching one costs
  // a TLS handshake to learn nothing. Checked AFTER thumbUrl() because Amazon's rewrite transcodes
  // PNG originals to JPEG, so only what we are actually about to request matters.
  //
  // This is not just wasted bytes. Spotify's placeholder icons live on a DIFFERENT host from its
  // artwork (spotify-static.ws.sonos.com vs i.scdn.co), and the fetcher keeps one pooled TLS
  // client, so a list mixing the two forced a fresh handshake on every alternation — the
  // connect/close churn plans/07 identifies as what wedges this board's ESP-Hosted link.
  if (small.endsWith(".png") || small.endsWith(".PNG")) return nullptr;

  Req r {};
  strncpy(r.key, stationKey.c_str(), sizeof r.key - 1);
  strncpy(r.url, small.c_str(), sizeof r.url - 1);
  xQueueSend(s_q, &r, 0);
  return nullptr;
}

}  // namespace artcache
