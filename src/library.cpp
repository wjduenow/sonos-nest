#include "library.h"
#include "player_state.h"
#include "sonos/didl.h"
#include "sonos/soap_client.h"

namespace library {

static String s_reqObject;        // pending browse object id ("" = none)
static int    s_mode = PLAY_FAVORITE;  // how to act on a selection in the current list
static bool   s_busy = false;

// One cached row: just the title (for display) plus the *fetch key* — the parent object and
// the row's true child index within it. At play time we re-browse (parent, childIdx, 1) to
// pull that one row's full DIDL (res + <r:resMD>) on demand. We keep the fetch key rather
// than the heavy metadata so a big list (69 favorites ≈ 116 KB) never has to be held at once.
struct Row { String title; String parent; uint32_t childIdx; };
static std::vector<Row> s_items;
static uint32_t s_gen = 0, s_consumed = 0;
static int    s_reqPlay = -1;
// The picked row's fetch key, captured on the UI thread the instant it's requested. We copy
// it out of s_items here (not later in netTask) because the UI clears s_items via
// clearResults() as soon as it leaves the browse screen — which happens before netTask runs.
static String   s_playParent;
static uint32_t s_playChild = 0;

void requestBrowse(const String &objectId, int playMode) {
  if (stateLock()) {
    s_reqObject = objectId;
    s_mode = playMode;
    s_busy = true;
    stateUnlock();
  }
}

bool busy() {
  bool b = false;
  if (stateLock()) { b = s_busy; stateUnlock(); }
  return b;
}

bool takeResults(std::vector<String> &labelsOut) {
  bool fresh = false;
  if (stateLock()) {
    if (s_gen != s_consumed) {
      s_consumed = s_gen;
      fresh = true;
      labelsOut.clear();
      for (auto &it : s_items) labelsOut.push_back(it.title);
    }
    stateUnlock();
  }
  return fresh;
}

void clearResults() {
  if (stateLock()) {
    std::vector<Row>().swap(s_items);  // free, not just clear (release capacity)
    stateUnlock();
  }
}

void requestPlay(int index) {
  if (stateLock()) {
    s_reqPlay = index;
    // Snapshot the fetch key now, while s_items is still populated (see s_playParent).
    if (index >= 0 && index < (int)s_items.size()) {
      s_playParent = s_items[index].parent;
      s_playChild  = s_items[index].childIdx;
    } else {
      s_playParent = "";
    }
    stateUnlock();
  }
}

// Paginate `object`, appending display rows to `out`. Pages are small so a huge list
// (69 favorites ≈ 116 KB) never has to fit in one response String on the tight SRAM heap.
// We keep only {title, parent, childIdx} — enough to re-fetch the picked row's full DIDL at
// play time. childIdx is the row's TRUE index within `object` (start + position in page), so
// it still points at the right child even when onlyPlaylists skips intervening items.
// When onlyPlaylists is set, keep only playlist-type items — the favorite's embedded
// <r:resMD> carries the real upnp:class, so we filter on "playlistContainer" with no round-trip.
static void collectRows(const String &ip, const String &object, bool onlyPlaylists,
                        std::vector<Row> &out) {
  const uint32_t PAGE = 16, MAX_ROWS = 200;
  for (uint32_t start = 0; start < MAX_ROWS && out.size() < MAX_ROWS; start += PAGE) {
    String didl;
    std::vector<sonos::DidlItem> page;
    if (!sonos::browse(ip, object, didl, start, PAGE)) break;
    size_t n = sonos::parseDidl(didl, page);
    for (uint32_t j = 0; j < page.size(); ++j) {
      if (onlyPlaylists && page[j].metadata.indexOf("playlistContainer") < 0) continue;
      out.push_back({page[j].title, object, start + j});
    }
    if (n < PAGE) break;  // short page => last page
  }
}

void service(const String &browseIp, const String &coordIp, const String &coordUuid) {
  // 1) Browse request. The Playlists screen (PLAY_PLAYLIST) is a merged list: Sonos saved
  // playlists (SQ:) plus playlist-type favorites (service playlists saved as favorites).
  // Other screens browse their single object. Only {id,title} is cached per row; the heavy
  // per-item DIDL (res + <r:resMD>) is fetched on demand at play time.
  String obj;
  int mode = PLAY_FAVORITE;
  if (stateLock()) { obj = s_reqObject; s_reqObject = ""; mode = s_mode; stateUnlock(); }
  if (obj.length()) {
    std::vector<Row> rows;
    if (mode == PLAY_PLAYLIST) {
      collectRows(browseIp, "SQ:",  false, rows);  // all Sonos saved playlists
      collectRows(browseIp, "FV:2", true,  rows);  // + playlist-type favorites only
    } else {
      collectRows(browseIp, obj, false, rows);     // Favorites (FV:2) / Queue (Q:0)
    }
    if (stateLock()) { s_items = rows; s_gen++; s_busy = false; stateUnlock(); }
  }

  // 2) Play request.
  int idx = -1;
  if (stateLock()) { idx = s_reqPlay; s_reqPlay = -1; stateUnlock(); }
  if (idx < 0) return;

  if (mode == PLAY_QUEUE) {
    // Queue: jump to track (idx is 0-based; Seek TRACK_NR is 1-based) and play.
    sonos::seekTrack(coordIp, (uint32_t)idx + 1);
    sonos::play(coordIp);
    return;
  }

  // Fetch just the picked row's full DIDL (res + metadata) on demand by re-browsing its
  // parent at its own child index (BrowseDirectChildren, window of 1). This returns the same
  // rich item the list showed — favorites keep their <r:resMD> (auth token), playlists keep
  // their enqueue res — without caching the heavy metadata for every row. (BrowseMetadata by
  // id does NOT reliably return the <res>/<r:resMD> for SQ:/FV:2 objects, so we don't use it.)
  String parent; uint32_t childIdx = 0;
  if (stateLock()) { parent = s_playParent; childIdx = s_playChild; stateUnlock(); }
  if (parent.length() == 0) return;

  sonos::DidlItem item;
  {
    String didl;
    std::vector<sonos::DidlItem> one;
    if (sonos::browse(browseIp, parent, didl, childIdx, 1)) sonos::parseDidl(didl, one);
    if (one.empty()) return;
    item = one[0];
  }

  // Collections (Sonos playlist = file:saved queue; service playlist/album = cpcontainer)
  // can't be set as the transport URI directly — enqueue them, then play from the queue.
  // Single streams/tracks/radio are set as the source directly.
  if (item.resUri.startsWith("x-rincon-cpcontainer:") || item.resUri.startsWith("file:")) {
    sonos::removeAllTracksFromQueue(coordIp);
    sonos::addUriToQueue(coordIp, item.resUri, item.metadata);
    sonos::setAvTransportUri(coordIp, "x-rincon-queue:" + coordUuid + "#0", "");
    sonos::play(coordIp);
  } else {
    sonos::setAvTransportUri(coordIp, item.resUri, item.metadata);
    sonos::play(coordIp);
  }
}

}  // namespace library
