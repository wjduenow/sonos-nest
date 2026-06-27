#include "library.h"
#include "player_state.h"
#include "sonos/didl.h"
#include "sonos/soap_client.h"

namespace library {

static String s_reqObject;        // pending browse object id ("" = none)
static int    s_mode = PLAY_FAVORITE;  // how to act on a selection in the current list
static bool   s_busy = false;
static std::vector<sonos::DidlItem> s_items;
static uint32_t s_gen = 0, s_consumed = 0;
static int    s_reqPlay = -1;

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
  // 1) Browse request.
  String obj;
  if (stateLock()) { obj = s_reqObject; s_reqObject = ""; stateUnlock(); }
  if (obj.length()) {
    String didl;
    std::vector<sonos::DidlItem> parsed;
    if (sonos::browse(browseIp, obj, didl)) sonos::parseDidl(didl, parsed);
    if (stateLock()) { s_items = parsed; s_gen++; s_busy = false; stateUnlock(); }
  }

  // 2) Play request.
  int idx = -1, mode = PLAY_FAVORITE;
  if (stateLock()) { idx = s_reqPlay; s_reqPlay = -1; mode = s_mode; stateUnlock(); }
  if (idx < 0) return;

  if (mode == PLAY_QUEUE) {
    // Queue: jump to track (idx is 0-based; Seek TRACK_NR is 1-based) and play.
    sonos::seekTrack(coordIp, (uint32_t)idx + 1);
    sonos::play(coordIp);
    return;
  }

  sonos::DidlItem item;
  bool ok = false;
  if (stateLock()) {
    if (idx < (int)s_items.size()) { item = s_items[idx]; ok = true; }
    stateUnlock();
  }
  if (!ok) return;

  if (mode == PLAY_PLAYLIST) {
    // Sonos playlist: clear the queue, enqueue it, point transport at the queue, play.
    sonos::removeAllTracksFromQueue(coordIp);
    sonos::addUriToQueue(coordIp, item.resUri, item.metadata);
    sonos::setAvTransportUri(coordIp, "x-rincon-queue:" + coordUuid + "#0", "");
    sonos::play(coordIp);
  } else {
    // Favorite / stream: set it as the source and play.
    sonos::setAvTransportUri(coordIp, item.resUri, item.metadata);
    sonos::play(coordIp);
  }
}

}  // namespace library
