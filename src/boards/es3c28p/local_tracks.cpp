// ES3C28P local track library — implements the board HAL's localTrack* (core/board.h) by
// scanning the microSD root for .mp3 files. The sleep_machine unit uses it to let the user
// pick which track options 2 (Sonos) and 3 (on-device speaker) play.
#include "core/board.h"
#include "sd_card.h"
#include <Arduino.h>
#include "SD_MMC.h"
#include <vector>

static std::vector<String> s_paths, s_names;
static bool s_scanned = false;

static void scan() {
  s_scanned = true;
  s_paths.clear();
  s_names.clear();
  if (!sdEnsureMounted()) return;
  File root = SD_MMC.open("/");
  if (!root) return;
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    bool dir = f.isDirectory();
    String name = f.name();   // 2.0.x: full path with leading slash; be defensive anyway
    f.close();
    if (dir) continue;
    String lower = name;
    lower.toLowerCase();
    if (!lower.endsWith(".mp3")) continue;
    String path = name.startsWith("/") ? name : (String("/") + name);
    int slash = path.lastIndexOf('/');
    String disp = path.substring(slash + 1);
    disp.remove(disp.length() - 4);   // drop the ".mp3" for a cleaner display name
    s_paths.push_back(path);
    s_names.push_back(disp);
  }
  root.close();
}

void localTracksRefresh() { s_scanned = false; }

int localTrackCount() {
  if (!s_scanned) scan();
  return (int)s_paths.size();
}

const char *localTrackName(int i) {
  if (!s_scanned) scan();
  return (i >= 0 && i < (int)s_names.size()) ? s_names[i].c_str() : nullptr;
}

const char *localTrackPath(int i) {
  if (!s_scanned) scan();
  return (i >= 0 && i < (int)s_paths.size()) ? s_paths[i].c_str() : nullptr;
}
