// Amazon Music (SMAPI service id 201) — account linking and station browsing.
//
// WHY THIS EXISTS AND WHAT IT IS NOT. Sonos cannot tell us anything about a music service's
// catalogue: the local ContentDirectory returns UPnP 701 for every third-party container, and the
// household's own OAuth token is stored on the players but is write-only. So a controller that
// wants to BROWSE a service has to become its own registered account. Amazon Music is
// `Auth="DeviceLink"`, which — unlike YouTube Music's AppLink — any client may initiate: we ask for
// a link code, the owner approves it in a browser once, and we keep our own token. Full evidence,
// and the list of routes that do NOT work, is in plans/08-music-service-integration.md.
//
// Everything here is BLOCKING HTTPS. Call it from the crawler task or netTask, never from uiTask.
#pragma once

#include <Arduino.h>
#include <vector>

namespace amazon {

// A browsable container (a genre, "Recently Played", "Popular Genres & Artists"...).
struct Genre {
  String title;
  String id;      // SMAPI object id, e.g. "catalog/stations/refinements/genres/<uuid>/#prime_stations"
};

// One playable station. `id` is the SMAPI object id and carries a server-minted "#chunk-<uuid>".
//
// *** Store `id` VERBATIM. Never split it, never rebuild it, never invent a #chunk. ***
// The chunk is minted fresh on every response — the same station browsed twice yields two different
// ones — but old values never expire (a favourite from years ago still plays). So a cached id stays
// good indefinitely, and there is no reason to reconstruct one.
struct Station {
  String title;
  String id;
  String artUrl;  // full-size; run through artThumbUrl() before fetching, or it may be a 6 MB PNG
};

// Fill in the two household-derived values linking and playback need — the Sonos household id and
// our Amazon account serial (the sn= URI parameter). Both are read from any discovered speaker, so
// this needs discovery to have run. Cheap, idempotent, and a no-op once both are known.
void adopt();

// --- Account linking ----------------------------------------------------------------------------
// Three-step ceremony, owner in the loop once:
//   1. linkBegin(url)  -> show `url` (or a QR of it) and ask the owner to approve in a browser
//   2. linkPoll()      -> call every few seconds; true once Amazon hands over the credentials
//   3. linked()        -> persists across reboots (NVS); nothing else needed until it is revoked
bool linked();
bool linkBegin(String &regUrlOut);
bool linkPoll();
void unlink();

// --- Browsing -----------------------------------------------------------------------------------
// Both return false on transport failure or a fault we cannot recover from. An expired token is
// NOT a failure: it is refreshed transparently and the call is retried once (see the .cpp).
bool genres(std::vector<Genre> &out);
bool stations(const String &genreId, std::vector<Station> &out);

// The Prime Stations root — the entry point for a full crawl.
extern const char *kStationsRoot;

// --- Playback ----------------------------------------------------------------------------------
// Turn a browsed station into what SetAVTransportURI wants. Verified on hardware: a constructed
// URI for a station never previously played on the household plays, with no getMediaURI step.
String playUri(const Station &s);
String playMeta(const Station &s, const String &genreId);

// --- Artwork -----------------------------------------------------------------------------------
// Amazon serves 2400x2400 originals, up to 6.7 MB, and ~40% of them are PNG — which TJpg cannot
// decode at all. Appending a resize op both shrinks them and transcodes to baseline JPEG:
// a 1,422,804 B PNG becomes a 1,988 B JPEG at px=128. ALWAYS fetch through this.
String artThumbUrl(const String &artUrl, int px = 128);

}  // namespace amazon
