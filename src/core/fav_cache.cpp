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

  // Page FV:2 in SMALL pages. 20, not 50: a favourite's record carries its whole <r:resMD> DIDL
  // (~700 bytes), so a 50-item page is a ~50 KB response held as a String and then COPIED again by
  // the unescape — and min internal heap was measured dropping to 41 KB with 50. Internal SRAM is
  // the scarce resource on this board; halving the page halves the spike for one extra round trip.
  std::vector<Fav> favs;
  int start = 0, total = 1, skipped = 0;
  while (start < total && favs.size() < 300) {
    String r;
    if (!sonos::soapAction(zs[0].ip, "/MediaServer/ContentDirectory/Control",
                           "urn:schemas-upnp-org:service:ContentDirectory:1", "Browse",
                           String("<ObjectID>FV:2</ObjectID><BrowseFlag>BrowseDirectChildren</BrowseFlag>"
                                  "<Filter>*</Filter><StartingIndex>") + start +
                           "</StartingIndex><RequestedCount>20</RequestedCount>"
                           "<SortCriteria></SortCriteria>", r)) {
      LOG.println("[favs  ] browse failed");
      s_busy = false; return false;
    }
    const String tm = tag(r, "TotalMatches");
    if (tm.length()) total = tm.toInt();
    const String didl = unesc(tag(r, "Result"));

    // Split on the record boundary, NOT on </item> — see the header comment.
    int got = 0, p = didl.indexOf("<item id=\"FV:2/");
    while (p >= 0) {
      const int nxt = didl.indexOf("<item id=\"FV:2/", p + 1);
      const String rec = (nxt < 0) ? didl.substring(p) : didl.substring(p, nxt);
      Fav f;
      f.title  = unesc(tag(rec, "dc:title"));
      f.uri    = unesc(tag(rec, "res"));
      f.artUrl = unesc(tag(rec, "upnp:albumArtURI"));
      f.meta   = tag(rec, "r:resMD");          // already single-unescaped; Sonos wants it as-is
      // A favourite with no <res> cannot be played — on this household "Discover Sonos Radio" and
      // "Sonos Presents" are both like this. Skip them, but say so: silently showing 40 of 42 with
      // no explanation is the kind of gap that gets reported as a bug.
      if (f.title.length() && f.uri.length()) favs.push_back(f);
      else if (f.title.length())              ++skipped;
      ++got;
      p = nxt;
    }
    if (!got) break;
    start += got;
  }
  if (favs.empty()) { LOG.println("[favs  ] no favourites returned"); s_busy = false; return false; }

  mkdir(root().c_str(), 0777);
  const String tmp = path() + ".tmp";
  FILE *f = fopen(tmp.c_str(), "wb");
  if (!f) { s_busy = false; return false; }
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
  for (const auto &v : favs) {
    buf += clean(v.title) + "\t" + clean(v.uri) + "\t" + clean(v.artUrl) + "\t" + clean(v.meta) + "\n";
    flush(false);
  }
  flush(true);
  fclose(f);
  if (!ok) { unlink(tmp.c_str()); s_busy = false; return false; }

  // Swap last, so an interrupted write never replaces a good cache with a partial one.
  unlink(path().c_str());
  rename(tmp.c_str(), path().c_str());
  s_count = -1;
  s_lastRefreshMs = millis();
  LOG.printf("[favs  ] cached %u favourite(s)%s\n", (unsigned)favs.size(),
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

static void favTask(void *) {
  for (;;) {
    if (localStorageRoot() && wifiIsConnected() && sonos::zoneCount()) {
      bool due = s_want || !ready();
      s_want = false;
      // Share the radio catalogue's daily slot: one overnight window for both keeps scheduled
      // traffic off the ESP-Hosted link at the times anyone is listening.
      const time_t now = time(nullptr);
      if (!due && settingsRadioAutoRefresh() && now > 1600000000) {
        struct tm lt {}, ft {};
        localtime_r(&now, &lt);
        const time_t fa = (time_t)fetchedAt();
        localtime_r(&fa, &ft);
        const bool ranToday = fetchedAt() && lt.tm_year == ft.tm_year && lt.tm_yday == ft.tm_yday;
        due = (lt.tm_hour == (int)settingsRadioRefreshHour()) && !ranToday;
      }
      if (due) refresh();
    }
    vTaskDelay(pdMS_TO_TICKS(15000));
  }
}

void start() { xTaskCreatePinnedToCore(favTask, "favcache", 8192, nullptr, 1, nullptr, 0); }

}  // namespace favcache
