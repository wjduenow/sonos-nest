// See radio_cache.h.
//
// ON-DISK LAYOUT (under <localStorageRoot()>/radio):
//   index.tsv   "v1<TAB>fetchedAt" then one line per genre:  <idx><TAB><title><TAB><object id>
//   gNN.tsv     one line per station, SORTED BY TITLE:       <title><TAB><id><TAB><artUrl>
//   all.tsv     flat search index:                           <title><TAB><genreIdx><TAB><id>
//
// Two simplifications against the sketch in plans/08, both deliberate:
//
//   * NO .azx letter-offset files. The A-Z jump strip needs a letter -> row mapping, but the UI has
//     already read the whole genre file to fill the carousel, so the offsets are two lines of code
//     over data that is in RAM anyway. A file would be a second thing to keep in sync for nothing.
//   * NO global sort of all.tsv. Sorting 1,044 Strings at once would cost ~150 KB of heap on a board
//     whose internal SRAM is the scarce resource, and search scans linearly regardless — match
//     quality decides result order, not file order. Each genre is still sorted, which is what the
//     carousel actually displays.
//
// WRITE RULES, measured on this hardware and not negotiable (plans/08):
//   * <= 4 KB per write call. Larger fails immediately — the card sticks in RCV state and rejects
//     the next command. Buf below never exceeds 4 KB.
//   * Keep any single file under ~256 KB; sustained writing dies past ~300 KB. all.tsv is the only
//     file that could approach this: ~95 B x ~1,044 = ~100 KB, so there is real headroom, but the
//     art URL is deliberately NOT in it — that would roughly double it.
#include "radio_cache.h"

#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "amazon.h"
#include "board.h"
#include "net/wifi.h"

namespace radiocache {

static const char *kSub     = "/radio";
static const char *kTmpSub  = "/radio.tmp";
static const uint32_t kMaxAgeS = 7 * 24 * 3600;   // weekly; genre lists are static, rosters crawl
static const uint32_t kPaceMs  = 1500;            // between requests — see the pacing note below

static bool     s_busy = false;
static uint32_t s_fetchedAt = 0;
static int      s_genreCount = -1;                // lazily read from index.tsv
static volatile bool s_wantRefresh = false;

// --- paths ---------------------------------------------------------------------------------------
static String root(bool tmp = false) {
  const char *r = localStorageRoot();
  if (!r) return "";
  return String(r) + (tmp ? kTmpSub : kSub);
}
static String genrePath(int idx, bool tmp = false) {
  char n[16]; snprintf(n, sizeof n, "/g%02d.tsv", idx);
  return root(tmp) + n;
}

// --- a writer that respects the 4 KB rule ---------------------------------------------------------
// Accumulates lines and flushes in <= 4 KB pieces. Every write on this board must go through
// something like this; a naive fwrite of a whole file wedges the card.
class Writer {
 public:
  explicit Writer(const String &path) { f_ = fopen(path.c_str(), "wb"); }
  ~Writer() { close(); }
  bool ok() const { return f_ != nullptr && !failed_; }
  void line(const String &s) {
    if (!f_ || failed_) return;
    buf_ += s; buf_ += '\n';
    if (buf_.length() >= 3072) flush();
  }
  bool close() {
    if (!f_) return false;
    flush();
    fclose(f_); f_ = nullptr;
    return !failed_;
  }
 private:
  void flush() {
    if (!f_ || buf_.isEmpty()) return;
    const char *p = buf_.c_str();
    size_t left = buf_.length();
    while (left && !failed_) {
      const size_t n = left > 4096 ? 4096 : left;      // *** the 4 KB rule ***
      if (fwrite(p, 1, n, f_) != n) { failed_ = true; break; }
      p += n; left -= n;
    }
    buf_ = "";
  }
  FILE  *f_ = nullptr;
  bool   failed_ = false;
  String buf_;
};

// --- small helpers --------------------------------------------------------------------------------
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
// Tabs and newlines are the record separators, so they can never appear in a value.
static String clean(String s) { s.replace('\t', ' '); s.replace('\n', ' '); s.replace('\r', ' '); return s; }

static void rmTree(const String &dir) {
  // Flat directory; no recursion needed. Unlink what we know we write, then the dir itself.
  unlink((dir + "/index.tsv").c_str());
  unlink((dir + "/all.tsv").c_str());
  for (int i = 0; i < 64; ++i) {
    char n[16]; snprintf(n, sizeof n, "/g%02d.tsv", i);
    unlink((dir + n).c_str());
  }
  rmdir(dir.c_str());
}

// --- state ----------------------------------------------------------------------------------------
static void loadHeader() {
  s_genreCount = 0; s_fetchedAt = 0;
  if (root().isEmpty()) return;
  FILE *f = fopen((root() + "/index.tsv").c_str(), "rb");
  if (!f) return;
  char buf[256];
  if (fgets(buf, sizeof buf, f)) {
    String h(buf); h.trim();
    if (field(h, 0) == "v1") s_fetchedAt = (uint32_t)strtoul(field(h, 1).c_str(), nullptr, 10);
  }
  int n = 0;
  while (fgets(buf, sizeof buf, f)) if (buf[0] != '\n' && buf[0] != '\r') ++n;
  fclose(f);
  s_genreCount = s_fetchedAt ? n : 0;
}

bool     ready()     { if (s_genreCount < 0) loadHeader(); return s_genreCount > 0; }
uint32_t fetchedAt() { if (s_genreCount < 0) loadHeader(); return s_fetchedAt; }
bool     busy()      { return s_busy; }
void     requestRefresh() { s_wantRefresh = true; }

// --- the crawl --------------------------------------------------------------------------------------
bool refresh() {
  if (root().isEmpty()) { Serial.println("[radio ] no storage — cache unavailable"); return false; }
  if (!amazon::linked()) { Serial.println("[radio ] Amazon not linked — cannot crawl"); return false; }
  if (s_busy) return false;
  s_busy = true;

  const String tmp = root(true);
  rmTree(tmp);
  mkdir(tmp.c_str(), 0777);

  std::vector<amazon::Genre> genres;
  if (!amazon::genres(genres)) {
    Serial.println("[radio ] genre browse failed");
    s_busy = false; return false;
  }
  Serial.printf("[radio ] crawling %u genres\n", (unsigned)genres.size());

  Writer all(tmp + "/all.tsv");
  Writer idx(tmp + "/index.tsv");
  idx.line(String("v1\t") + String((uint32_t)time(nullptr)));

  int totalStations = 0;
  for (size_t g = 0; g < genres.size(); ++g) {
    // PACING IS DELIBERATE. 27 requests back to back is ~500 KB in a burst, which is exactly the
    // sustained-load profile that kills the ESP-Hosted link. A crawl that takes a minute is free;
    // a wedged radio is not.
    vTaskDelay(pdMS_TO_TICKS(kPaceMs));

    std::vector<amazon::Station> st;
    if (!amazon::stations(genres[g].id, st)) {
      Serial.printf("[radio ]   %-24s FAILED — skipping\n", genres[g].title.c_str());
      continue;   // a genre we cannot read is a gap, not a reason to discard the whole crawl
    }
    // Sorted here so neither the device nor the A-Z strip has to sort later.
    std::sort(st.begin(), st.end(), [](const amazon::Station &a, const amazon::Station &b) {
      return strcasecmp(a.title.c_str(), b.title.c_str()) < 0;
    });

    Writer gf(genrePath((int)g, true));
    for (const auto &s : st) {
      gf.line(clean(s.title) + "\t" + clean(s.id) + "\t" + clean(s.artUrl));
      all.line(clean(s.title) + "\t" + String((int)g) + "\t" + clean(s.id));
    }
    if (!gf.close()) {
      Serial.printf("[radio ]   %-24s WRITE FAILED\n", genres[g].title.c_str());
      s_busy = false; return false;
    }
    idx.line(String((int)g) + "\t" + clean(genres[g].title) + "\t" + clean(genres[g].id));
    totalStations += (int)st.size();
    Serial.printf("[radio ]   %-24s %2u stations\n", genres[g].title.c_str(), (unsigned)st.size());
  }

  const bool okAll = all.close();
  const bool okIdx = idx.close();
  if (!okAll || !okIdx) {
    Serial.println("[radio ] index write failed — keeping the previous cache");
    s_busy = false; return false;
  }

  // Swap last. Until this point the live cache is untouched, so a crawl that dies partway leaves
  // the previous index intact rather than a half-written one.
  rmTree(root());
  if (rename(tmp.c_str(), root().c_str()) != 0) {
    Serial.println("[radio ] rename failed — cache left in the temp tree");
    s_busy = false; return false;
  }
  s_genreCount = -1;   // force a re-read
  Serial.printf("[radio ] cache built: %d genres, %d stations\n", (int)genres.size(), totalStations);
  s_busy = false;
  return true;
}

// --- reading ------------------------------------------------------------------------------------
int genreCount() { if (s_genreCount < 0) loadHeader(); return s_genreCount < 0 ? 0 : s_genreCount; }

bool genre(int idx, String &titleOut, String &idOut) {
  if (!ready() || idx < 0 || idx >= genreCount()) return false;
  FILE *f = fopen((root() + "/index.tsv").c_str(), "rb");
  if (!f) return false;
  char buf[512];
  bool found = false;
  fgets(buf, sizeof buf, f);                    // header
  for (int i = 0; fgets(buf, sizeof buf, f); ++i) {
    if (i != idx) continue;
    String l(buf); l.trim();
    titleOut = field(l, 1); idOut = field(l, 2);
    found = titleOut.length() > 0;
    break;
  }
  fclose(f);
  return found;
}

bool stations(int genreIdx, std::vector<Station> &out) {
  if (!ready()) return false;
  FILE *f = fopen(genrePath(genreIdx).c_str(), "rb");
  if (!f) return false;
  char buf[512];
  while (fgets(buf, sizeof buf, f)) {
    String l(buf); l.trim();
    if (l.isEmpty()) continue;
    Station s;
    s.title = field(l, 0); s.id = field(l, 1); s.artUrl = field(l, 2);
    if (s.title.length() && s.id.length()) out.push_back(s);
  }
  fclose(f);
  return !out.empty();
}

int search(const String &query, std::vector<Hit> &out, int max) {
  if (!ready() || query.isEmpty()) return 0;
  String q = query; q.toLowerCase();
  FILE *f = fopen((root() + "/all.tsv").c_str(), "rb");
  if (!f) return 0;
  char buf[512];
  int n = 0;
  while (n < max && fgets(buf, sizeof buf, f)) {
    String l(buf); l.trim();
    if (l.isEmpty()) continue;
    String t = field(l, 0), lower = t; lower.toLowerCase();
    if (lower.indexOf(q) < 0) continue;
    Hit h;
    h.title = t;
    h.genreIdx = field(l, 1).toInt();
    h.id = field(l, 2);
    out.push_back(h);
    ++n;
  }
  fclose(f);
  return n;
}

// --- background task ------------------------------------------------------------------------------
static void cacheTask(void *) {
  for (;;) {
    // Wait for the preconditions rather than failing on them: on first boot the card may be absent,
    // Wi-Fi down, and the Amazon account unlinked, and all three can arrive later.
    if (localStorageRoot() && wifiIsConnected()) amazon::adopt();
    if (localStorageRoot() && wifiIsConnected() && amazon::linked()) {
      const uint32_t now = (uint32_t)time(nullptr);
      const bool stale = !ready() ||
                         (now > 1600000000 && fetchedAt() && now - fetchedAt() > kMaxAgeS);
      if (s_wantRefresh || stale) {
        s_wantRefresh = false;
        refresh();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(30000));
  }
}

void start() {
  // Core 0 alongside the network, low priority: this is a background chore that must never
  // compete with rendering. Stack is generous because the crawl holds one genre's worth of
  // Strings plus a TLS session.
  xTaskCreatePinnedToCore(cacheTask, "radiocache", 8192, nullptr, 1, nullptr, 0);
}

}  // namespace radiocache
