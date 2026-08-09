// See fav_cache.h.
//
// ON-DISK: <localStorageRoot()>/favs/index.tsv
//   line 1  "v1<TAB>fetchedAt"
//   then    <title><TAB><uri><TAB><artUrl><TAB><resMD DIDL>
//
// One file, because there are ~40 favourites and no hierarchy — the per-genre split that radio_cache
// needs would be pure ceremony here. At ~900 bytes a row that is ~38 KB, comfortably inside the
// ~256 KB single-file limit this board's SD writes impose (plans/08).
//
// PARSING TRAP, and it is the one that matters: an FV:2 item CONTAINS A NESTED <item> inside its
// <r:resMD>, so the obvious `<item>.*?</item>` match stops at the inner close and silently truncates
// every record — which is exactly how research here first concluded "this favourite has no
// metadata". Records are split on the `<item id="FV:2/` boundary instead.
#include "fav_cache.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "board.h"
#include "net/wifi.h"
#include "settings.h"
#include "sonos/soap_client.h"
#include "sonos/ssdp.h"
#include "net/logmirror.h"   // LOG — tees to the TCP mirror where enabled, plain Serial otherwise
#include "heap_watch.h"   // heapwatch::note — attribute the heap low-water (heap_watch.h)

namespace favcache {

static const uint32_t kFreshMs = 5 * 60 * 1000;   // on-entry refresh window
static bool     s_busy = false;
static int      s_count = -1;
static uint32_t s_fetchedAt = 0;
static uint32_t s_lastRefreshMs = 0;
static volatile bool s_want = false;

static String root() {
  const char *r = localStorageRoot();
  return r ? String(r) + "/favs" : String();
}
static String path() { return root() + "/index.tsv"; }

static String field(const String &line, int n) {
  int start = 0;
  for (int i = 0; i < n; ++i) {
    start = line.indexOf('\t', start);
    if (start < 0) return "";
    ++start;
  }
  const int end = line.indexOf('\t', start);
  return (end < 0) ? line.substring(start) : line.substring(start, end);
}
static String clean(String s) { s.replace('\t', ' '); s.replace('\n', ' '); s.replace('\r', ' '); return s; }

static String unesc(String s) {
  s.replace("&lt;", "<"); s.replace("&gt;", ">"); s.replace("&quot;", "\"");
  s.replace("&apos;", "'"); s.replace("&amp;", "&");
  return s;
}
// Attribute- and prefix-tolerant, for the same reasons spelled out in amazon.cpp.
static String tag(const String &xml, const char *name) {
  const String open = String("<") + name;
  int p = xml.indexOf(open);
  while (p >= 0) {
    const char c = xml.charAt(p + open.length());
    if (c == '>' || c == ' ') break;
    p = xml.indexOf(open, p + 1);
  }
  if (p < 0) return "";
  const int gt = xml.indexOf('>', p);
  const int e  = xml.indexOf(String("</") + name + ">", gt);
  return (gt < 0 || e < 0) ? "" : xml.substring(gt + 1, e);
}

static void loadHeader() {
  s_count = 0; s_fetchedAt = 0;
  if (root().isEmpty()) return;
  FILE *f = fopen(path().c_str(), "rb");
  if (!f) return;
  char buf[1200];
  if (fgets(buf, sizeof buf, f)) {
    String h(buf); h.trim();
    if (field(h, 0) == "v1") s_fetchedAt = (uint32_t)strtoul(field(h, 1).c_str(), nullptr, 10);
  }
  int n = 0;
  while (fgets(buf, sizeof buf, f)) if (buf[0] != '\n' && buf[0] != '\r') ++n;
  fclose(f);
  s_count = s_fetchedAt ? n : 0;
}

bool     ready()     { if (s_count < 0) loadHeader(); return s_count > 0; }
uint32_t fetchedAt() { if (s_count < 0) loadHeader(); return s_fetchedAt; }
bool     busy()      { return s_busy; }
int      count()     { if (s_count < 0) loadHeader(); return s_count < 0 ? 0 : s_count; }
void     requestRefresh() { s_want = true; }
bool     stale()     { return !ready() || !s_lastRefreshMs || (millis() - s_lastRefreshMs) > kFreshMs; }

bool refresh() {
  if (root().isEmpty() || s_busy) return false;
  std::vector<sonos::Zone> zs;
  sonos::zonesSnapshot(zs);
  if (zs.empty()) return false;
  s_busy = true;

  // Page FV:2 in SMALL pages. 10 — and this is not conservatism, it is measured. A favourite's
  // record carries its whole <r:resMD> DIDL (~700 bytes), and the page is held as a String and
  // then COPIED again by the unescape. 50 took min internal heap to 41 KB. 20 was still enough,
  // in combination with everything else running, to take FREE heap to 25 KB and MIN to 1.4 KB on
  // a live device — past the point where LWIP cannot get socket buffers, at which point the board
  // answers ping and nothing else, and recovers only by rebooting. Each halving costs one extra
  // round trip and nothing else. Do not raise it.
  // STREAM STRAIGHT TO DISK. This used to accumulate every record in a std::vector<Fav> and write
  // the file afterwards — ~38 KB of Strings held for the whole crawl, stacked underneath each
  // page's own transients. Measured with heapwatch: the refresh took free heap from ~110 KB to a
  // minimum of 42 KB, and with the old 20-item pages it reached 1.4 KB and killed the device.
  // Nothing needs the whole list in memory: the file is the output, so write each record as it is
  // parsed and never hold more than one page.
  mkdir(root().c_str(), 0777);
  const String tmp = path() + ".tmp";
  FILE *f = fopen(tmp.c_str(), "wb");
  if (!f) { s_busy = false; return false; }

  // Still "v1". A version bump WAS tried, to force stale caches (which held escaped r:resMD) to
  // re-fetch — and rejecting the old version is what forced a refresh during the boot storm and
  // put the device in a boot loop. The live cache has since been rebuilt correctly, so there is
  // nothing to migrate; if a future format change ever does need one, force it from a settled
  // device, never from ready()==false at boot.
  String buf = String("v1\t") + String((uint32_t)time(nullptr)) + "\n";
  bool ok = true;
  auto flush = [&](bool force) {
    if (!ok || (!force && buf.length() < 3072)) return;
    const char *q = buf.c_str(); size_t left = buf.length();
    while (left && ok) {                       // <= 4 KB per write — the rule from plans/08
      const size_t n = left > 4096 ? 4096 : left;
      if (fwrite(q, 1, n, f) != n) ok = false;
      q += n; left -= n;
    }
    buf = "";
  };
  auto bail = [&]() { fclose(f); unlink(tmp.c_str()); s_busy = false; return false; };

  int start = 0, total = 1, skipped = 0, kept = 0;
  while (start < total && kept < 300 && ok) {
    String didl;
    {
      // r is scoped so the raw response is released before the record loop runs — it is the same
      // size again as didl, and holding both through the parse doubled the page's cost.
      String r;
      if (!sonos::soapAction(zs[0].ip, "/MediaServer/ContentDirectory/Control",
                             "urn:schemas-upnp-org:service:ContentDirectory:1", "Browse",
                             String("<ObjectID>FV:2</ObjectID><BrowseFlag>BrowseDirectChildren</BrowseFlag>"
                                    "<Filter>*</Filter><StartingIndex>") + start +
                             "</StartingIndex><RequestedCount>10</RequestedCount>"
                             "<SortCriteria></SortCriteria>", r)) {
        LOG.println("[favs  ] browse failed");
        return bail();
      }
      const String tm = tag(r, "TotalMatches");
      if (tm.length()) total = tm.toInt();
      didl = unesc(tag(r, "Result"));
    }
    heapwatch::note("favs.page");     // page held, raw response already released

    // Split on the record boundary, NOT on </item> — see the header comment.
    int got = 0, p = didl.indexOf("<item id=\"FV:2/");
    while (p >= 0) {
      const int nxt = didl.indexOf("<item id=\"FV:2/", p + 1);
      const String rec = (nxt < 0) ? didl.substring(p) : didl.substring(p, nxt);
      const String title  = unesc(tag(rec, "dc:title"));
      const String uri    = unesc(tag(rec, "res"));
      const String artUrl = unesc(tag(rec, "upnp:albumArtURI"));
      // *** unesc() HERE IS LOAD-BEARING. *** `didl` has had ONE unescape (of Browse's Result);
      // the r:resMD inside it is escaped a second time, exactly like the three fields above, which
      // is why they all call unesc() too. Storing it still-escaped meant soapAction escaped it
      // AGAIN on the way out, so the speaker received `&lt;DIDL-Lite` as literal text.
      //
      // Proven on hardware, same favourite back to back: without this, AddURIToQueue answers HTTP
      // 500 / UPnP errorCode 800; with it, 364 tracks are added. It only broke SOME favourites,
      // which is what disguised it as a service problem: a station URI (x-sonosapi-radio:) plays
      // from the URI alone and ignores bad metadata, but a CONTAINER (x-rincon-cpcontainer:, which
      // is what all 28 YouTube Music favourites here are) can only be resolved THROUGH it.
      // didl.cpp's parseDidl() always did this correctly; fav_cache parses separately and did not.
      const String meta = unesc(tag(rec, "r:resMD"));
      // A favourite with no <res> cannot be played — on this household "Discover Sonos Radio" and
      // "Sonos Presents" are both like this. Skip them, but say so: silently showing 40 of 42 with
      // no explanation is the kind of gap that gets reported as a bug.
      if (title.length() && uri.length()) {
        buf += clean(title) + "\t" + clean(uri) + "\t" + clean(artUrl) + "\t" + clean(meta) + "\n";
        ++kept;
        flush(false);
      } else if (title.length()) {
        ++skipped;
      }
      ++got;
      p = nxt;
    }
    if (!got) break;
    start += got;
  }
  flush(true);
  if (!ok || !kept) {
    if (!kept) LOG.println("[favs  ] no favourites returned");
    return bail();
  }
  fclose(f);

  // Swap last, so an interrupted write never replaces a good cache with a partial one.
  unlink(path().c_str());
  rename(tmp.c_str(), path().c_str());
  s_count = -1;
  s_lastRefreshMs = millis();
  LOG.printf("[favs  ] cached %d favourite(s)%s\n", kept,
                skipped ? String(String(", skipped ") + skipped + " with no playable URI").c_str() : "");
  s_busy = false;
  return true;
}

bool all(std::vector<Fav> &out) {
  if (!ready()) return false;
  FILE *f = fopen(path().c_str(), "rb");
  if (!f) return false;
  char buf[1200];
  fgets(buf, sizeof buf, f);                    // header
  while (fgets(buf, sizeof buf, f)) {
    String l(buf); l.trim();
    if (l.isEmpty()) continue;
    Fav v;
    v.title = field(l, 0); v.uri = field(l, 1); v.artUrl = field(l, 2); v.meta = field(l, 3);
    if (v.title.length() && v.uri.length()) out.push_back(v);
  }
  fclose(f);
  return !out.empty();
}

int search(const String &query, std::vector<Fav> &out, int max) {
  std::vector<Fav> everything;
  if (!all(everything)) return 0;
  String q = query; q.toLowerCase();
  int n = 0;
  for (const auto &v : everything) {
    if (n >= max) break;
    String t = v.title; t.toLowerCase();
    if (t.indexOf(q) < 0) continue;
    out.push_back(v); ++n;
  }
  return n;
}

// A refresh is by far the heaviest thing this module does, and on this board it runs close to the
// edge: measured on a live device it took free internal heap to 25 KB and MIN to 1.4 KB. Below
// ~15 KB LWIP cannot get socket buffers, the device answers ping and nothing else, and the only
// recovery is a reboot. Two guards, both from watching that happen:
//
//   kSettleMs  Never refresh during the boot storm. LVGL, the art cache, the radio cache, GENA and
//              the web server all come up in the first minute; a burst of SOAP and SD on top of
//              that is what tips it over. This is also what makes the failure SELF-SUSTAINING —
//              the reboot interrupts the refresh before it can write a valid cache, so !ready() is
//              still true next boot and it tries again, forever. Deferring breaks that cycle.
//
//   kMinHeap   Refuse outright when heap is already low, wherever that came from. A stale
//              favourites list is a cosmetic problem; taking the whole device down is not.
//
// kMinHeap MUST sit BELOW this board's idle free heap, or the refresh can never run at all. The
// first attempt used 90 KB, taken from a reading seconds after boot — but the device settles at
// ~87 KB free, so every refresh was silently deferred forever and the cache could never be
// rebuilt. Idle ~87 KB, a 10-item page wants ~30 KB of headroom, the cliff is ~15 KB: 55 KB clears
// the cliff with margin and still lets the guard actually fire. If you raise it, check it against
// a device that has been up for a while, not one that just booted.
static const uint32_t kSettleMs = 90000;
static const uint32_t kMinHeap  = 55000;

static void favTask(void *) {
  for (;;) {
    if (localStorageRoot() && wifiIsConnected() && sonos::zoneCount()) {
      bool due = s_want || !ready();
      s_want = false;
      if (due && millis() < kSettleMs) {
        due = false;                       // not now — retried on a later pass
      } else if (due && ESP.getFreeHeap() < kMinHeap) {
        LOG.printf("[favs  ] refresh deferred, only %lu B heap free\n",
                   (unsigned long)ESP.getFreeHeap());
        due = false;
      }
      // Favourites have their OWN daily slot now, rather than sharing the radio catalogue's.
      // They are cheap and change often (anything edited in the Sonos app); the station crawl is
      // expensive and changes rarely, so one schedule could not suit both. Keeping them on
      // SEPARATE hours also matters on this board: two heavy refreshes in the same hour is more
      // internal SRAM pressure than it can take (see kMinHeap above). Defaults are 04:00 radio,
      // 05:00 favourites.
      const time_t now = time(nullptr);
      if (!due && settingsFavAutoRefresh() && now > 1600000000) {
        struct tm lt {}, ft {};
        localtime_r(&now, &lt);
        const time_t fa = (time_t)fetchedAt();
        localtime_r(&fa, &ft);
        const bool ranToday = fetchedAt() && lt.tm_year == ft.tm_year && lt.tm_yday == ft.tm_yday;
        due = (lt.tm_hour == (int)settingsFavRefreshHour()) && !ranToday;
      }
      if (due) refresh();
    }
    vTaskDelay(pdMS_TO_TICKS(15000));
  }
}

void start() { xTaskCreatePinnedToCore(favTask, "favcache", 8192, nullptr, 1, nullptr, 0); }

}  // namespace favcache
