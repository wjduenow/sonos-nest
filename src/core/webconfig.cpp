// Remote-config surface — see webconfig.h. Runs on whatever task the board's HTTP server uses,
// so every write goes through the same guarded paths the UI task uses: NVS for the persisted
// picks, and g_pending (under stateLock) for anything netTask has to act on.
#include "webconfig.h"
#include "settings.h"
#include "player_state.h"   // g_pending + stateLock/stateUnlock — the cross-task command channel
#include "board.h"          // localTrack* — the HAL's view of local storage (empty on most boards)
#include "sonos/ssdp.h"
#include "net/wifi.h"      // wifiHostname() — the effective name the router shows
#include "net/ota.h"       // otaHostname()  — the mDNS name, which is NOT the same thing
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

// Playlist-name cache, published by the unit after its "SQ:" browse — see webconfig.h.
static std::vector<String> s_playlists;
void webConfigPlaylistsSet(const std::vector<String> &names) { s_playlists = names; }

// Sanitize to a valid DHCP/mDNS hostname: letters, digits and hyphens; spaces and underscores
// become hyphens; no leading/trailing hyphen. Returns "" if nothing usable survives.
//
// NOTE: units/sleep_machine/screens.cpp:764-775 has an identical copy for its on-device keyboard.
// The two must agree — a name typed on the sleep-machine and one typed on a config page should
// sanitize the same way. If you change the rule, change it in both (or lift this into settings.*
// and have that unit call it, which needs testing on the es3c28p hardware).
static String sanitizeHostname(const String &raw) {
  String h;
  for (unsigned i = 0; i < raw.length(); ++i) {
    char c = raw[i];
    bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (alnum)                                 h += c;
    else if (c == ' ' || c == '_' || c == '-') h += '-';
  }
  while (h.startsWith("-")) h.remove(0, 1);
  while (h.endsWith("-"))   h.remove(h.length() - 1);
  return h;
}

// Strict 0..100: digits only, non-empty, in range.
static bool parsePct(const String &value, long &out) {
  if (value.length() == 0) return false;
  for (unsigned i = 0; i < value.length(); ++i) if (!isdigit((int)value[i])) return false;
  out = value.toInt();
  return out >= 0 && out <= 100;
}

String webConfigJson() {
  JsonDocument doc;
  doc["sleepTrack"] = settingsSleepTrack();
  doc["wakeTrack"]  = settingsWakeTrack();
  doc["room"]       = settingsRoom();
  doc["ring"]       = settingsRing();
  doc["playlist"]   = settingsPlaylist();
  doc["volume"]     = settingsVolume();
  // The EFFECTIVE name (falls back to the firmware default when nothing is stored), not the raw
  // NVS value — a page showing "" when the router says "sonos-button" would just be wrong.
  doc["deviceName"] = wifiHostname();
  // The mDNS/OTA name is compile-time and does NOT follow deviceName. The page says so, because
  // assuming otherwise sends you looking for a "<your-name>.local" that never resolves.
  doc["mdnsName"]   = String(otaHostname()) + ".local";

  JsonArray pls = doc["playlists"].to<JsonArray>();
  for (const String &n : s_playlists) pls.add(n);

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

  if (field == "ring" || field == "volume") {
    // toInt() yields 0 on garbage, and 0 is legitimate for both of these, so validate the text
    // rather than trusting the parse — otherwise "banana" silently means "off"/"silent".
    long v;
    if (!parsePct(value, v)) { err = field + " must be a number 0..100"; return false; }
    if (field == "ring") settingsSetRing((uint8_t)v);   // the unit applies it via webConfigGen
    else                 settingsSetVolume((uint8_t)v); // read at press time; nothing to apply
    s_gen++;
    return true;
  }

  if (field == "playlist") {
    if (value.length() == 0) { err = "pick a playlist"; return false; }
    // Validate against the browsed list when we have one: a typo'd name fails at 2am with the
    // button doing nothing, which is the worst possible time to discover it. Before the first
    // browse lands the cache is empty — accept then, rather than refusing to configure at all.
    if (!s_playlists.empty()) {
      bool known = false;
      for (const String &n : s_playlists) if (n == value) { known = true; break; }
      if (!known) { err = "no such playlist"; return false; }
    }
    settingsSetPlaylist(value);
    s_gen++;   // signal the unit to re-warm its play-by-name cache for the new pick
    return true;
  }

  if (field == "deviceName") {
    String h = sanitizeHostname(value);
    if (h.length() == 0) { err = "name needs at least one letter or digit"; return false; }
    // RFC 1035 caps a label at 63; keep it well under so mDNS and router UIs stay sane.
    if (h.length() > 32) h = h.substring(0, 32);
    settingsSetDeviceName(h);
    // Reconnect so DHCP re-registers under the new name. Blocking, on netTask — the page's
    // response has already been decided by then, but the device drops off the network for a
    // moment, so the browser's follow-up poll may miss once. That's expected, not a fault.
    if (stateLock()) { g_pending.reconnectWifi = true; stateUnlock(); }
    return true;
  }

  err = "unknown field";
  return false;
}

void webConfigTrackDeleted(const String &path) {
  if (settingsSleepTrack() == path) settingsSetSleepTrack("");
  if (settingsWakeTrack()  == path) settingsSetWakeTrack("");
}
