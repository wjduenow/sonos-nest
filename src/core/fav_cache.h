// Sonos favourites (FV:2), cached to local storage — the same strategy as radio_cache, with one
// deliberate difference.
//
// A station catalogue is a thousand entries Amazon controls and that change slowly, so a daily
// crawl is the whole story. FAVOURITES ARE FORTY-ODD ENTRIES THE OWNER EDITS THEMSELVES, in the
// Sonos app, and expects to see on the panel straight away. A purely scheduled refresh would mean
// adding a favourite and not seeing it until 4am tomorrow.
//
// So this refreshes on three triggers rather than one:
//   * on entering the page, if the cache is older than kFreshMs (a few minutes) — self-heals
//     without anyone pressing anything
//   * on the daily schedule, shared with the radio cache
//   * on demand, from the screen or the web config page
//
// The cache still earns its place: browsing is instant, it survives a reboot and a dead link, and
// the cached URI + DIDL let a favourite play without re-browsing FV:2 first.
#pragma once

#include <Arduino.h>
#include <vector>

namespace favcache {

struct Fav {
  String title;
  String uri;      // res URI, played as-is
  String meta;     // its <r:resMD> DIDL — the reason playing needs no browse
  String artUrl;   // upnp:albumArtURI; empty on the few favourites that have none
};

bool     ready();
uint32_t fetchedAt();
bool     busy();
bool     stale();          // older than the on-entry freshness window

bool refresh();            // blocking browse of FV:2 -> storage. Cache task only, never the UI.
void start();
void requestRefresh();

int  count();
bool all(std::vector<Fav> &out);
int  search(const String &query, std::vector<Fav> &out, int max);

}  // namespace favcache
