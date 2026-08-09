// Amazon Prime Stations, crawled to local storage so browsing is instant and survives a dead link.
//
// WHY A CACHE. Browsing live costs one HTTPS SOAP round trip per level at ~0.6 s, over the
// ESP-Hosted link that is this board's one unresolved fault under sustained load. A full crawl is
// 27 requests / ~500 KB / ~16 s once a week; after that every tap reads a small file.
//
// Device-agnostic: with no storage (localStorageRoot() == nullptr) or no Amazon link, everything
// here degrades to "not ready" and the UI says so. Nothing in it is jukebox-specific.
#pragma once

#include <Arduino.h>
#include <vector>

namespace radiocache {

struct Station {
  String title;
  String id;       // SMAPI object id, stored VERBATIM — see amazon.h on why never to rebuild it
  String artUrl;   // full-size; run through amazon::artThumbUrl() before fetching
};

struct Hit {       // a search result: which genre, and where in it
  String title;
  String id;
  int    genreIdx;
};

// --- state --------------------------------------------------------------------------------------
bool     ready();          // a usable index exists on disk
uint32_t fetchedAt();      // unix seconds of the last successful crawl, 0 if never
bool     busy();           // a crawl is running right now

// --- the crawl ----------------------------------------------------------------------------------
// Blocking, ~30-60 s, paced deliberately (see the .cpp). Call from the cache task, never the UI.
// Writes to a temp tree and swaps at the end, so an interrupted crawl never leaves a partial index.
bool refresh();

// Start the background task that refreshes on boot when the index is stale, and on request.
// Safe to call when there is no storage or no link — it waits and retries rather than failing.
void start();
void requestRefresh();     // manual trigger, e.g. a Settings button

// --- reading ------------------------------------------------------------------------------------
// All cheap: each call reads one small file. Safe from the UI task.
int  genreCount();
bool genre(int idx, String &titleOut, String &idOut);
bool stations(int genreIdx, std::vector<Station> &out);

// Substring match over every station, case-insensitive. Capped by `max` because the caller renders
// into the LVGL pool. Scans one flat file rather than 26.
int  search(const String &query, std::vector<Hit> &out, int max);

}  // namespace radiocache
