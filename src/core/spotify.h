// Spotify (SMAPI service id 12, type 3079 = 12*256+7) — account linking, browse and SEARCH.
//
// WHY THIS IS POSSIBLE AT ALL. `plans/08` recorded for a long time that a standalone controller
// could only link `DeviceLink` services and that AppLink ones were shut. That was inferred from
// YouTube Music's failure and never tested; it is wrong. An anonymous `getAppLink` against
// Spotify's endpoint returns a live registration URL and link code, the owner approves it once in a
// browser, and `getDeviceAuthToken` hands over a token this device keeps. Run end to end on
// 2026-09-04, with `search` verified against all four categories.
//
// WHAT THIS IS FOR: finding ids. Search returns native `spotify:track:` / `:album:` / `:artist:` /
// `:playlist:` ids, which are the transparent-wrapper form Sonos accepts — so the jukebox can offer
// a real search box without a Pi, a cloud service or the Sonos app.
//
// ⚠️ THE SEARCH ACCOUNT AND THE PLAYBACK ACCOUNT ARE DIFFERENT ACCOUNTS, AND THAT IS FINE. The token
// here only browses. Playback goes to the speaker, which resolves `x-sonos-spotify:` with the
// Spotify account linked in the Sonos app (the `sn=` serial). So a search UI does not depend on
// this token for playback — only for listing.
//
// ⚠️ Everything except linked()/linkState() is BLOCKING HTTPS. Call from netTask or the link task,
// never from uiTask: a stalled UI task on the jukebox is indistinguishable from a dead Wi-Fi link
// (CLAUDE.md, the `[health]` heartbeat), and this is exactly how you would cause one.
#pragma once

#include <Arduino.h>
#include <vector>

namespace spotify {

// One row of a browse or a search result.
struct Item {
  enum class Kind : uint8_t { Track, Artist, Album, Playlist, Container };

  String title;
  String subtitle;    // artist, or a playlist's owner. May be empty.
  String id;          // the NATIVE id, verbatim: "spotify:track:6vLa…". Never rebuild one.
  String artUrl;      // may be empty
  Kind   kind = Kind::Container;
};

// Which search index to hit. The strings these map to are per-service — Spotify wants `track`
// where YouTube Music wants `SONGS` — and both come from the service's presentation map, so a
// second service is a table entry rather than new code. See kCategoryId in the .cpp.
enum class Category : uint8_t { All, Tracks, Artists, Albums, Playlists };

// --- account -------------------------------------------------------------------------------------

bool linked();          // a token is in NVS. Says nothing about whether it still works.
void unlink();

// The ceremony, owner in the loop once: linkStart(), then show linkUrl() (as a QR — the URL is far
// too long to read off a wall panel) and poll linkState().
//
// ⚠️ SPOTIFY'S LINK CODE EXPIRES IN ~5 MINUTES, and it expires SILENTLY IN THE BROWSER: the SOAP
// side keeps answering NOT_LINKED_RETRY while the web page has already started saying "The link
// code you entered is not valid." Measured 2026-09-04: minted 09:29:20, still retryable at
// 09:34:05, invalid at 09:34:10. So a UI must show linkSecondsLeft() and re-mint on expiry rather
// than leaving a dead code on screen. Amazon's window is far longer (>25 min observed); do not
// share a constant between them.
enum class LinkState : uint8_t {
  Idle,       // nothing in progress
  Starting,   // asking Spotify for a code
  Waiting,    // show linkUrl() and wait for the owner to approve in a browser
  Linked,     // token persisted
  Failed,     // no code, or the window closed unused
};
void      linkStart();
void      linkCancel();
LinkState linkState();
String    linkUrl();
uint16_t  linkSecondsLeft();

// --- reading (blocking) --------------------------------------------------------------------------

// Browse a container. `id` is "root" or an id returned by a previous call, verbatim.
bool browse(const String &id, std::vector<Item> &out, int index = 0, int count = 30);

// Search one category. `count` is a request, not a promise.
bool search(const String &term, Category cat, std::vector<Item> &out, int count = 8);

// Drop the pooled TLS session once a burst is done — see smapi.h on why only one should be open.
void endSession();

// --- playback ------------------------------------------------------------------------------------

// Fill in the household-derived values playback needs (the Sonos household id, and our Spotify
// account serial read from an existing favourite). Cheap, idempotent, needs discovery to have run.
void adopt();

// A transport URI + DIDL for `it`, to hand to g_pending.playUri / .playMeta.
//
// ⚠️ TRACKS ARE PROVEN, CONTAINERS ARE NOT. The household has a working `x-sonos-spotify:` track
// favourite to copy the shape from; albums, playlists and artists need the
// `x-rincon-cpcontainer:<8-hex prefix>` form, and that prefix is not derivable from anything we can
// read — plans/08's wrapper rule says how to READ one, not how to mint one for a type we have no
// sample of. playUri() returns "" for a Kind it cannot construct, so a caller must check.
String playUri(const Item &it);
String playMeta(const Item &it);

}  // namespace spotify
