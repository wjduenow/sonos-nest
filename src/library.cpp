#include "library.h"
#include "player_state.h"
#include "sonos/didl.h"
#include "sonos/soap_client.h"

namespace library {

static String s_reqObject;        // pending browse object id ("" = none)
static String s_rootObject;       // the current list's root (for just-in-time item fetch)
static int    s_mode = PLAY_FAVORITE;  // how to act on a selection in the current list
static bool   s_busy = false;
static std::vector<sonos::DidlItem> s_items;  // title-only rows (metadata fetched on play)
static uint32_t s_gen = 0, s_consumed = 0;
static int    s_reqPlay = -1;

void requestBrowse(const String &objectId, int playMode) {
  if (stateLock()) {
    s_reqObject = objectId;
    s_rootObject = objectId;
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

void requestPlay(int index) {
  if (stateLock()) { s_reqPlay = index; stateUnlock(); }
}

void clearResults() {
  if (stateLock()) {
    std::vector<sonos::DidlItem>().swap(s_items);  // free, not just clear (release capacity)
    stateUnlock();
  }
}

void service(const String &browseIp, const String &coordIp, const String &coordUuid) {
  // 1) Browse request. Paginate: a full list can be far too big to hold in one response
  // String on the tight SRAM heap (69 favorites ≈ 116 KB), which left Favorites blank.
  // Fetch in small pages and keep only the row titles — the heavy per-item metadata
  // (<r:resMD>) is fetched on demand in step 2 for just the row the user picks.
  String obj;
  if (stateLock()) { obj = s_reqObject; s_reqObject = ""; stateUnlock(); }
  if (obj.length()) {
    std::vector<sonos::DidlItem> rows;
    const uint32_t PAGE = 16, MAX_ROWS = 200;
    for (uint32_t start = 0; start < MAX_ROWS; start += PAGE) {
      String didl;
      std::vector<sonos::DidlItem> page;
      if (!sonos::browse(browseIp, obj, didl, start, PAGE)) break;
      size_t n = sonos::parseDidl(didl, page);
      for (auto &it : page) {
        sonos::DidlItem row; row.title = it.title;  // title only — bound the heap
        rows.push_back(row);
      }
      if (n < PAGE) break;  // short page => last page
    }
    if (stateLock()) { s_items = rows; s_gen++; s_busy = false; stateUnlock(); }
  }

  // 2) Play request.
  int idx = -1, mode = PLAY_FAVORITE;
  String root;
  if (stateLock()) { idx = s_reqPlay; s_reqPlay = -1; mode = s_mode; root = s_rootObject; stateUnlock(); }
  if (idx < 0) return;

  if (mode == PLAY_QUEUE) {
    // Queue: jump to track (idx is 0-based; Seek TRACK_NR is 1-based) and play.
    sonos::seekTrack(coordIp, (uint32_t)idx + 1);
    sonos::play(coordIp);
    return;
  }

  // Fetch just the picked row's full DIDL (res + metadata) on demand — keeps the cached
  // list tiny and gives favorites their <r:resMD> (auth token) without storing it for all.
  sonos::DidlItem item;
  {
    String didl;
    std::vector<sonos::DidlItem> one;
    if (sonos::browse(browseIp, root, didl, (uint32_t)idx, 1)) sonos::parseDidl(didl, one);
    if (one.empty()) return;
    item = one[0];
  }

  if (mode == PLAY_PLAYLIST) {
    // Sonos playlist: clear the queue, enqueue it, point transport at the queue, play.
    sonos::removeAllTracksFromQueue(coordIp);
    sonos::addUriToQueue(coordIp, item.resUri, item.metadata);
    sonos::setAvTransportUri(coordIp, "x-rincon-queue:" + coordUuid + "#0", "");
    sonos::play(coordIp);
  } else if (item.resUri.startsWith("x-rincon-cpcontainer:")) {
    // Container favorite (e.g. a YouTube Music playlist/album): can't be set as the transport
    // URI directly — enqueue it like a Sonos playlist, then point transport at the queue.
    sonos::removeAllTracksFromQueue(coordIp);
    sonos::addUriToQueue(coordIp, item.resUri, item.metadata);
    sonos::setAvTransportUri(coordIp, "x-rincon-queue:" + coordUuid + "#0", "");
    sonos::play(coordIp);
  } else {
    // Single stream/track favorite: set it as the source and play.
    sonos::setAvTransportUri(coordIp, item.resUri, item.metadata);
    sonos::play(coordIp);
  }
}

}  // namespace library
