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
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "amazon.h"
#include "board.h"
#include "net/wifi.h"
#include "settings.h"
#include "net/logmirror.h"   // LOG — tees to the TCP mirror where enabled, plain Serial otherwise
#include "heap_watch.h"   // heapwatch::note — attribute the heap low-water (heap_watch.h)

namespace radiocache {

static const char *kSub     = "/radio";
static const char *kTmpSub  = "/radio.tmp";
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
static bool dirExists(const String &p) {
  DIR *d = opendir(p.c_str());
  if (!d) return false;
  closedir(d);
  return true;
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
// Reduce any Amazon station id to its durable STATION KEY, which is the only part that identifies
// the station across crawls. THIS IS NOT COSMETIC. The three forms in circulation are
//
//   catalog/stations/<KEY>/#chunk-<uuid>     browse + favourites, older credential
//   prime/stations/<KEY>/#chunk-<uuid>       legacy namespace, same key space
//   catalog:station:key:<KEY>                browse, newer credential
//
// and the `#chunk-` is **minted fresh on every response** (amazon.h says so in capitals). Hashing
// the whole id therefore makes every station look new on every crawl: the merge preserves the
// entire previous cache, and the genre count grows by one crawl's worth per refresh — 26 -> 52 ->
// 78, with the Radio page showing each station several times. Observed on hardware before this
// existed. Keying on the last path/colon segment before the fragment also makes the prime/ and
// catalog/ spellings of one station compare equal, which they are.
static String stationKey(const String &id) {
  int end = id.indexOf('#');
  if (end < 0) end = (int)id.length();
  while (end > 0 && (id[end - 1] == '/' || id[end - 1] == ':')) --end;
  int start = end;
  while (start > 0 && id[start - 1] != '/' && id[start - 1] != ':') --start;
  return id.substring(start, end);
}

// FNV-1a over that key. Used only for "have I already got this one?" — see the merge in refresh()
// for why a hash and not the string itself.
static uint32_t idHash(const String &id) {
  const String k = stationKey(id);
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < k.length(); ++i) { h ^= (uint8_t)k[i]; h *= 16777619u; }
  return h;
}
// Tabs and newlines are the record separators, so they can never appear in a value.
static String clean(String s) { s.replace('\t', ' '); s.replace('\n', ' '); s.replace('\r', ' '); return s; }

// Genuinely recursive, and it has to be. This used to unlink only the flat files it knew it wrote,
// which is fine right up until something else puts a SUBDIRECTORY in the tree — the artwork cache
// used to live at radio/art. A surviving subdirectory makes rmdir() fail, so the destination still
// exists, so the rename() swap below fails with "rename failed — cache left in the temp tree", and
// the crawl never publishes no matter how many times it succeeds. Recursing also migrates devices
// that still carry that legacy radio/art directory.
static void rmTree(const String &dir) {
  if (DIR *d = opendir(dir.c_str())) {
    while (struct dirent *e = readdir(d)) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      const String p = dir + "/" + e->d_name;
      struct stat st;
      if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) rmTree(p);
      else                                                  unlink(p.c_str());
    }
    closedir(d);
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
bool     refreshPending() { return s_wantRefresh; }

// --- the crawl --------------------------------------------------------------------------------------
//
// RESUMABLE, ACROSS REBOOTS. Each genre lands in its own gNN.tsv in the temp tree as it arrives,
// and the temp tree is NOT wiped on entry — a run only fetches the genres whose file is missing.
//
// This exists because of the ESP-Hosted fault (plans/07): the link dies under sustained crawl load
// and the firmware reboots to recover. A crawl that restarted from genre 0 every time could never
// finish, so the cache was never written, so !ready() started another crawl on the next boot — an
// endless reboot loop that left the device unusable. Resuming converges instead: each pass gets
// further, and two or three passes complete it.
//
// The manifest (genres.tsv) guards the resume. gNN files are keyed by POSITION in the genre list,
// so if Amazon's list changes between passes the part-built tree is meaningless and is discarded.
static bool fileHasContent(const String &p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && st.st_size > 0;
}

static bool manifestMatches(const String &path, const std::vector<amazon::Genre> &genres) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return false;
  char buf[512];
  size_t i = 0;
  bool ok = true;
  while (fgets(buf, sizeof buf, f)) {
    String l(buf); l.trim();
    if (l.isEmpty()) continue;
    if (i >= genres.size() || field(l, 2) != clean(genres[i].id)) { ok = false; break; }
    ++i;
  }
  fclose(f);
  return ok && i == genres.size();
}

static bool writeManifest(const String &path, const std::vector<amazon::Genre> &genres) {
  Writer w(path);
  for (size_t i = 0; i < genres.size(); ++i)
    w.line(String((int)i) + "\t" + clean(genres[i].title) + "\t" + clean(genres[i].id));
  return w.close();
}

bool refresh() {
  if (root().isEmpty()) { LOG.println("[radio ] no storage — cache unavailable"); return false; }
  // Power loss between the two renames of a previous swap leaves the cache aside under .bak and
  // nothing at the live path. Put it back before doing anything else, or the crawl below would
  // merge against an empty cache and the .bak would be deleted on the way out.
  if (!dirExists(root()) && dirExists(root() + ".bak")) {
    if (rename((root() + ".bak").c_str(), root().c_str()) == 0) {
      s_genreCount = -1;
      LOG.println("[radio ] recovered the cache from an interrupted swap");
    }
  }
  if (!amazon::linked()) { LOG.println("[radio ] Amazon not linked — cannot crawl"); return false; }
  if (s_busy) return false;
  s_busy = true;

  const String tmp = root(true);
  mkdir(tmp.c_str(), 0777);

  std::vector<amazon::Genre> genres;
  if (!amazon::genres(genres)) {
    LOG.println("[radio ] genre browse failed");
    amazon::endSession();
    s_busy = false; return false;
  }

  // Resume only against an identical genre list; otherwise start clean.
  const String manifest = tmp + "/genres.tsv";
  if (!manifestMatches(manifest, genres)) {
    rmTree(tmp);
    mkdir(tmp.c_str(), 0777);
    if (!writeManifest(manifest, genres)) {
      LOG.println("[radio ] could not write the resume manifest");
      amazon::endSession();
      s_busy = false; return false;
    }
  }

  int have = 0;
  for (size_t g = 0; g < genres.size(); ++g)
    if (fileHasContent(genrePath((int)g, true))) ++have;
  if (have) LOG.printf("[radio ] resuming: %d of %u genres already cached\n",
                          have, (unsigned)genres.size());
  else      LOG.printf("[radio ] crawling %u genres\n", (unsigned)genres.size());

  int fetched = 0;
  for (size_t g = 0; g < genres.size(); ++g) {
    if (fileHasContent(genrePath((int)g, true))) continue;      // already have it

    // PACING IS DELIBERATE. Requests back to back are ~500 KB in a burst, which is exactly the
    // sustained-load profile that kills the ESP-Hosted link. A crawl that takes a minute is free;
    // a wedged radio is not.
    vTaskDelay(pdMS_TO_TICKS(kPaceMs));

    std::vector<amazon::Station> st;
    if (!amazon::stations(genres[g].id, st)) {
      LOG.printf("[radio ]   %-24s FAILED — will retry next pass\n", genres[g].title.c_str());
      continue;
    }
    // Sorted here so neither the device nor the A-Z strip has to sort later.
    std::sort(st.begin(), st.end(), [](const amazon::Station &a, const amazon::Station &b) {
      return strcasecmp(a.title.c_str(), b.title.c_str()) < 0;
    });
    // Peak of a crawl step: up to ~50 Stations, each three heap Strings, all live until the
    // Writer below drains them. amazon.body covers the fetch; this covers what it leaves behind.
    heapwatch::note("radio.crawl");

    Writer gf(genrePath((int)g, true));
    for (const auto &s : st) gf.line(clean(s.title) + "\t" + clean(s.id) + "\t" + clean(s.artUrl));
    if (!gf.close()) {
      LOG.printf("[radio ]   %-24s WRITE FAILED\n", genres[g].title.c_str());
      amazon::endSession();
      s_busy = false; return false;
    }
    ++fetched;
    LOG.printf("[radio ]   %-24s %2u stations\n", genres[g].title.c_str(), (unsigned)st.size());
  }
  amazon::endSession();   // the burst is over; do not hold a socket open on this link

  int missing = 0;
  for (size_t g = 0; g < genres.size(); ++g)
    if (!fileHasContent(genrePath((int)g, true))) ++missing;

  // A SHAPE CHANGE IS NOT A TRANSIENT FAILURE, and the rule below cannot tell them apart. If not
  // one container yielded a station — this pass or any earlier one — the tree we are crawling is
  // not the tree this code was written for, and publishing would rmTree a good cache and swap in an
  // empty index. Observed for real: Amazon flattened its station root in 2026-09 from 26 genre
  // containers to a flat list of 100 playable stations (itemType=program, canEnumerate=false), so
  // genres() now returns stations, every stations() call finds zero collections, and the crawl
  // "succeeds" with nothing. Keep what is on the card; a stale cache beats an empty one, and every
  // id in it still resolves. NOT `fetched == 0` on its own: a fully resumed pass legitimately
  // fetches nothing because every genre file is already there.
  if ((size_t)missing == genres.size()) {
    LOG.printf("[radio ] %u container(s), none yielded stations — keeping the existing cache "
               "(browse tree changed?)\n", (unsigned)genres.size());
    s_busy = false; return false;
  }

  // Convergence rule. Still missing genres but we made progress this pass -> keep the temp tree and
  // finish next time. Missing and NO progress -> nothing more to gain by waiting, so publish with
  // the gaps rather than never publishing at all.
  if (missing && fetched) {
    LOG.printf("[radio ] pass complete: %d fetched, %d still missing — resuming next run\n",
                  fetched, missing);
    s_busy = false; return false;
  }
  if (missing) LOG.printf("[radio ] publishing with %d genre(s) unavailable\n", missing);

  // --- preserve stations that have LEFT the tree --------------------------------------------------
  // Amazon's flattening shrank the browsable catalogue from ~1,045 stations to 100, and the ids it
  // stopped listing still PLAY — they are unreachable, not dead (verified against this household's
  // years-old favourites). A plain swap deletes them permanently, because nothing can ever
  // enumerate them again. So publishing is a MERGE: the crawled genres first, then whatever the
  // live cache holds that this crawl did not return, kept under the genre it was filed in.
  //
  // Keeping that genre structure is a MEMORY limit, not sentiment. Merged into one flat list,
  // 1,000+ stations become 1,000+ LVGL rows at ~1 KB each against a 512 KB pool — and pool
  // exhaustion on this board is a UI freeze, not a dropped frame (CLAUDE.md). Per-genre lists stay
  // at ~50 rows, which is what the page was built for.
  //
  // Membership is tested by 32-bit FNV-1a hash rather than by holding the ids: 1,000 Strings is
  // ~40 KB of internal heap on a board whose largest free block is ~32 KB, and a collision costs
  // one dropped station, never a corrupt cache.
  std::vector<uint32_t> seen;
  for (size_t g = 0; g < genres.size(); ++g) {
    FILE *f = fopen(genrePath((int)g, true).c_str(), "rb");
    if (!f) continue;
    char buf[512];
    while (fgets(buf, sizeof buf, f)) {
      String l(buf); l.trim();
      if (!l.isEmpty()) seen.push_back(idHash(field(l, 1)));
    }
    fclose(f);
  }

  const int crawled = (int)genres.size();
  std::vector<amazon::Genre> preserved;
  int keptStations = 0;
  for (int o = 0, oldN = genreCount(); o < oldN; ++o) {     // the LIVE cache; untouched until the swap
    String otitle, oid;
    if (!genre(o, otitle, oid)) continue;
    FILE *f = fopen(genrePath(o).c_str(), "rb");
    if (!f) continue;
    const String slotPath = genrePath(crawled + (int)preserved.size(), true);
    int n = 0;
    {
      Writer w(slotPath);
      char buf[512];
      while (fgets(buf, sizeof buf, f)) {
        String l(buf); l.trim();
        if (l.isEmpty()) continue;
        const uint32_t h = idHash(field(l, 1));
        if (std::find(seen.begin(), seen.end(), h) != seen.end()) continue;
        seen.push_back(h);
        w.line(l);            // already "title \t id \t artUrl" — copied verbatim, artwork included
        ++n;
      }
      if (!w.close()) n = 0;
    }
    fclose(f);
    if (n == 0) { unlink(slotPath.c_str()); continue; }     // wholly superseded, or unwritable
    amazon::Genre pg; pg.title = otitle; pg.id = oid;
    preserved.push_back(pg);
    keptStations += n;
  }
  if (keptStations) {
    LOG.printf("[radio ] preserved %d station(s) in %u genre(s) no longer in the tree\n",
               keptStations, (unsigned)preserved.size());
    genres.insert(genres.end(), preserved.begin(), preserved.end());
  }

  // Build the index and the flat search file from what is on the card. Done here, not during the
  // fetch, so a crawl spread over several passes still produces one consistent pair.
  int totalStations = 0;
  {
    Writer idx(tmp + "/index.tsv");
    idx.line(String("v1\t") + String((uint32_t)time(nullptr)));
    for (size_t g = 0; g < genres.size(); ++g)
      idx.line(String((int)g) + "\t" + clean(genres[g].title) + "\t" + clean(genres[g].id));
    if (!idx.close()) {
      LOG.println("[radio ] index write failed — keeping the previous cache");
      s_busy = false; return false;
    }

    Writer all(tmp + "/all.tsv");
    for (size_t g = 0; g < genres.size(); ++g) {
      FILE *f = fopen(genrePath((int)g, true).c_str(), "rb");
      if (!f) continue;
      char buf[512];
      while (fgets(buf, sizeof buf, f)) {
        String l(buf); l.trim();
        if (l.isEmpty()) continue;
        all.line(field(l, 0) + "\t" + String((int)g) + "\t" + field(l, 1));
        ++totalStations;
      }
      fclose(f);
    }
    if (!all.close()) {
      LOG.println("[radio ] search index write failed — keeping the previous cache");
      s_busy = false; return false;
    }
  }
  // Swap last. Until this point the live cache is untouched, so a crawl that dies partway leaves
  // the previous index intact rather than a half-written one.
  //
  // The live tree is moved ASIDE, not deleted. It used to be rmTree'd first, and the failure path
  // below said so out loud — "cache left in the temp tree" means the device now has no cache at
  // all. That was survivable when a re-crawl could rebuild everything; since the merge above, the
  // live tree holds stations that NOTHING can enumerate again, so losing it to a failed rename
  // would be permanent. Kept until the new tree is in place, then dropped.
  const String bak = root() + ".bak";
  rmTree(bak);
  const bool hadLive = dirExists(root()) && rename(root().c_str(), bak.c_str()) == 0;
  if (rename(tmp.c_str(), root().c_str()) != 0) {
    LOG.println("[radio ] rename failed — restoring the previous cache");
    if (hadLive) rename(bak.c_str(), root().c_str());
    s_busy = false; return false;
  }
  rmTree(bak);
  // Only now is the resume state safe to discard: dropping it before the swap would mean a failed
  // rename lost the whole part-built tree and the next pass started from genre 0 again.
  unlink((root() + "/genres.tsv").c_str());
  s_genreCount = -1;   // force a re-read
  LOG.printf("[radio ] cache built: %d genres, %d stations\n", (int)genres.size(), totalStations);
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
  // Held by the CALLER (the Radio page keeps it for the whole browse), so the note goes here
  // while it is full rather than after it is handed over -- see heap_watch.h.
  heapwatch::note("radio.stations");
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
      // Scheduling: a fixed LOCAL hour once a day, not an age timer. ~500 KB of crawl traffic on
      // a link that is this board's known weak point belongs at 4am, not whenever a 24h timer
      // happens to expire — which drifts into the evening within a week.
      //
      // "Already ran today" is derived from the cache's own fetchedAt rather than a counter, so it
      // survives a reboot: rebooting at 04:30 must not trigger a second crawl.
      const time_t now = time(nullptr);
      bool due = !ready();                       // no cache at all: build one immediately
      if (!due && settingsRadioAutoRefresh() && now > 1600000000) {
        struct tm lt {}, ft {};
        localtime_r(&now, &lt);
        const time_t f = (time_t)fetchedAt();
        localtime_r(&f, &ft);
        const bool ranToday = fetchedAt() && lt.tm_year == ft.tm_year && lt.tm_yday == ft.tm_yday;
        due = (lt.tm_hour == (int)settingsRadioRefreshHour()) && !ranToday;
      }
      if (s_wantRefresh || due) {
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
