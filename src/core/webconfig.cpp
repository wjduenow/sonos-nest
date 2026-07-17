// Remote-config surface — see webconfig.h. Runs on whatever task the board's HTTP server uses,
// so every write goes through the same guarded paths the UI task uses: NVS for the persisted
// picks, and g_pending (under stateLock) for anything netTask has to act on.
#include "webconfig.h"
#include "settings.h"
#include "player_state.h"   // g_pending + stateLock/stateUnlock — the cross-task command channel
#include "board.h"          // localTrack* — the HAL's view of local storage (empty on most boards)
#include "sonos/ssdp.h"
#include <ArduinoJson.h>

// Is this a real track on the card right now? Guards against a stale path from a page that was
// left open while the file was deleted.
static bool knownTrack(const String &path) {
  int n = localTrackCount();
  for (int i = 0; i < n; ++i) {
    const char *p = localTrackPath(i);
    if (p && path == p) return true;
  }
  return false;
}

// Bumped on every change a unit has to act on locally — see webconfig.h.
static uint32_t s_gen = 0;
uint32_t webConfigGen() { return s_gen; }

String webConfigJson() {
  JsonDocument doc;
  doc["sleepTrack"] = settingsSleepTrack();
  doc["wakeTrack"]  = settingsWakeTrack();
  doc["room"]       = settingsRoom();
  doc["ring"]       = settingsRing();

  JsonArray tracks = doc["tracks"].to<JsonArray>();
  int n = localTrackCount();
  for (int i = 0; i < n; ++i) {
    const char *name = localTrackName(i);
    const char *path = localTrackPath(i);
    if (!name || !path) continue;
    JsonObject t = tracks.add<JsonObject>();
    t["name"] = name;
    t["path"] = path;
  }

  // Zones are whatever discovery has found so far — possibly none, if it hasn't run yet.
  JsonArray zones = doc["zones"].to<JsonArray>();
  for (const sonos::Zone &z : sonos::zones()) {
    JsonObject o = zones.add<JsonObject>();
    o["name"] = z.name;
    o["ip"]   = z.ip;
  }

  String out;
  serializeJson(doc, out);
  return out;
}

bool webConfigApply(const String &field, const String &value, String &err) {
  if (field == "sleepTrack" || field == "wakeTrack") {
    if (value.length() && !knownTrack(value)) { err = "no such track"; return false; }
    if (field == "sleepTrack") settingsSetSleepTrack(value);   // "" => back to the unit default
    else                       settingsSetWakeTrack(value);    // "" => back to auto-detect
    return true;
  }

  if (field == "room") {
    for (const sonos::Zone &z : sonos::zones()) {
      if (z.name != value) continue;
      settingsSetRoom(z.name);                                 // persist the choice...
      if (stateLock()) {                                       // ...and let netTask switch to it
        g_pending.requestZoneIp = z.ip;
        stateUnlock();
      }
      return true;
    }
    err = "no such room";
    return false;
  }

  if (field == "ring") {
    // toInt() yields 0 on garbage, and 0 is a legitimate value ("off"), so validate the text
    // rather than trusting the parse — otherwise "banana" silently turns the ring off.
    for (unsigned i = 0; i < value.length(); ++i) {
      if (!isdigit((int)value[i])) { err = "ring must be a number 0..100"; return false; }
    }
    if (value.length() == 0) { err = "ring must be a number 0..100"; return false; }
    long v = value.toInt();
    if (v < 0 || v > 100) { err = "ring must be 0..100"; return false; }
    settingsSetRing((uint8_t)v);
    s_gen++;                 // the unit picks this up in uiTick and drives the pin
    return true;
  }

  err = "unknown field";
  return false;
}

void webConfigTrackDeleted(const String &path) {
  if (settingsSleepTrack() == path) settingsSetSleepTrack("");
  if (settingsWakeTrack()  == path) settingsSetWakeTrack("");
}
