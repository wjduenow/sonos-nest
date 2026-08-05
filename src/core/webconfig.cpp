// Remote-config surface — see webconfig.h. Runs on whatever task the board's HTTP server uses,
// so every write goes through the same guarded paths the UI task uses: NVS for the persisted
// picks, and g_pending (under stateLock) for anything netTask has to act on.
#include "webconfig.h"
#include "settings.h"
#include "player_state.h"   // g_pending + stateLock/stateUnlock — the cross-task command channel
#include "board.h"          // localTrack* — the HAL's view of local storage (empty on most boards)
#include "sonos/ssdp.h"
#include "sonos/soap_client.h"   // soapDiag() — runtime SOAP counters for the health readout
#include "net/wifi.h"      // wifiHostname() — the effective name the router shows
#include "net/ota.h"       // otaHostname()  — the mDNS name, which is NOT the same thing
#include "net/updater.h"   // updaterAvailable*/Approve/ForceCheck — the OTA pull path (plans/06)
#include <ArduinoJson.h>
#include <Arduino.h>       // ESP.getFreeHeap() etc. for the health readout
#include <esp_system.h>    // esp_reset_reason() — diagnose why the LAST reset happened
#include <WiFi.h>          // WiFi.localIP() — the registration payload's ip field

// Firmware version — injected per build by tools/git_version.py (git describe). Default lets a
// bare `pio run` compile; the real string arrives via the -DFW_VERSION build flag.
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

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

// Last LVGL pool sample reported by the unit (see webconfig.h). Plain uint32/uint8 stores written
// by the UI task and read by the HTTP task — a torn read just skews one diagnostic number, so no
// lock needed.
static uint32_t s_lvUsed = 0, s_lvMaxUsed = 0;
static uint8_t  s_lvFragPct = 0;
void webConfigReportLvMem(uint32_t usedBytes, uint32_t maxUsedBytes, uint8_t fragPct) {
  s_lvUsed = usedBytes; s_lvMaxUsed = maxUsedBytes; s_lvFragPct = fragPct;
}

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
  doc["brightness"] = settingsBrightness();   // screen units (nest); the unit applies changes
  doc["playlist"]   = settingsPlaylist();
  doc["volume"]     = settingsVolume();
  // Radio catalogue refresh. Belongs in the CONFIG document, not the registration payload — that
  // one is identity only (who and where), and putting settings there means the config page cannot
  // read back what it just wrote.
  doc["radio_refresh_hour"] = settingsRadioRefreshHour();
  doc["radio_auto_refresh"] = settingsRadioAutoRefresh();
  // The EFFECTIVE name (falls back to the firmware default when nothing is stored), not the raw
  // NVS value — a page showing "" when the router says "sonos-button" would just be wrong.
  doc["deviceName"] = wifiHostname();
  // The mDNS/OTA name now DOES follow deviceName (otaHostname() derives from it). It updates on
  // the reboot a name change triggers, so between the change and that boot this still reports the
  // currently-advertised name — which is what "<name>.local" actually resolves to right now.
  doc["mdnsName"]   = String(otaHostname()) + ".local";

  // Health readout for diagnosing the "slower the longer it runs" report. A falling heapFree (esp.
  // heapMin) points at a memory leak; a climbing soapReconnects / soapMaxMs at socket churn. Steady
  // across a long uptime = fixed. uptimeSec lets you read the trend without a reboot to compare.
  JsonObject h = doc["health"].to<JsonObject>();
  h["uptimeSec"] = (uint32_t)(millis() / 1000);
  h["heapFree"]  = (uint32_t)ESP.getFreeHeap();
  h["heapMin"]   = (uint32_t)ESP.getMinFreeHeap();
  h["resetReason"] = (int)esp_reset_reason();   // 4=PANIC 6=TASK_WDT 9=BROWNOUT (esp_reset_reason_t)
  uint32_t sCalls, sRe, sLast, sMax;
  sonos::soapDiag(sCalls, sRe, sLast, sMax);
  h["soapCalls"]      = sCalls;
  h["soapReconnects"] = sRe;
  h["soapLastMs"]     = sLast;
  h["soapMaxMs"]      = sMax;
  // LVGL pool usage (see webConfigReportLvMem). lvMemMax is the peak since boot — the number to
  // size LV_MEM_SIZE against. 0 until the UI task reports its first sample.
  h["lvMemUsed"]    = s_lvUsed;
  h["lvMemMax"]     = s_lvMaxUsed;
  h["lvMemFragPct"] = s_lvFragPct;

  // OTA pull state (net/updater.cpp): the toggle, the source, what's running, and what's available
  // (null when up-to-date/disabled). Drives the config page's "Updates" section and its Approve
  // button (shown only when available && !auto).
  JsonObject ota = doc["ota"].to<JsonObject>();
  ota["auto"]       = settingsOtaAuto();
  ota["updateUrl"]  = settingsUpdateUrl();          // raw stored: "" (auto) | "off" | a URL
  ota["source"]     = updaterEffectiveUrl();        // the resolved URL ("" when off)
  ota["sourceKind"] = updaterSourceKind();          // portal | github | custom | off
  ota["running"]    = FW_VERSION;
  if (updaterAvailable()) ota["available"] = updaterAvailableVersion();
  else                    ota["available"] = (const char *)nullptr;

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
  std::vector<sonos::Zone> zsnap;
  sonos::zonesSnapshot(zsnap);   // this server runs on its own task
  for (const sonos::Zone &z : zsnap) {
    JsonObject o = zones.add<JsonObject>();
    o["name"] = z.name;
    o["ip"]   = z.ip;
  }

  String out;
  serializeJson(doc, out);
  return out;
}

String registrationJson() {
  JsonDocument doc;
  doc["deviceName"] = wifiHostname();                       // effective router hostname
  doc["mdnsName"]   = String(otaHostname()) + ".local";     // stable id + "<name>.local" address
  doc["ip"]         = WiFi.localIP().toString();
  // Unit/board are compile-time — the same macro that selects the board+unit in the env. The
  // button is HEADLESS with neither UNIT_ macro; keep this branch order in sync with that.
#if defined(UNIT_NEST)
  doc["unit"] = "nest";    doc["board"] = "crowpanel_rotary";
#elif defined(UNIT_SLEEP)
  doc["unit"] = "sleep";   doc["board"] = "es3c28p";
#elif defined(HEADLESS)
  doc["unit"] = "button";  doc["board"] = "esp32s3cam";
#else
  doc["unit"] = "unknown"; doc["board"] = "unknown";
#endif
  doc["fwVersion"]  = FW_VERSION;
  // OTA pull status for the dashboard: the per-device policy + whether an update is waiting, so the
  // portal can show a version diff and offer an Approve button (plans/06 Phase 3).
  doc["otaAuto"] = settingsOtaAuto();
  if (updaterAvailable()) doc["updateAvailable"] = updaterAvailableVersion();
  else                    doc["updateAvailable"] = (const char *)nullptr;
  // boardConfigUrl() is the device's web-config page, or nullptr on boards without one (the nest).
  // NOT localManagerUrl() — that's specifically a *file* manager, which the button lacks even
  // though it serves a config page. Emit null so the portal renders "Open config" disabled.
  const char *cfg = boardConfigUrl();
  if (cfg) doc["configUrl"] = cfg;
  else     doc["configUrl"] = (const char *)nullptr;

  JsonArray zones = doc["zones"].to<JsonArray>();
  std::vector<sonos::Zone> zsnap;
  sonos::zonesSnapshot(zsnap);   // this server runs on its own task
  for (const sonos::Zone &z : zsnap) {
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

  // Radio catalogue refresh schedule. Exposed here rather than in a board's own page so every

  // board with a config UI gets it identically; boards without one simply never call this.

  if (field == "radio_refresh_hour") {

    const int h = value.toInt();

    // Fill err: the page renders it verbatim, and "Could not save:" with nothing after it
    // is worse than no message at all.
    if (h < 0 || h > 23) { err = "hour must be 0-23"; return false; }

    settingsSetRadioRefreshHour((uint8_t)h);

    return true;

  }

  if (field == "radio_auto_refresh") {

    settingsSetRadioAutoRefresh(value == "1" || value == "true" || value == "on");

    return true;

  }

  if (field == "room") {
    std::vector<sonos::Zone> zsnap;
    sonos::zonesSnapshot(zsnap);   // this server runs on its own task
    for (const sonos::Zone &z : zsnap) {
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

  if (field == "brightness") {
    // Screen backlight % (nest). settingsSetBrightness() floors at 10 so a slip can't blank the
    // display; the unit re-reads and applies it via webConfigGen (same path as ring).
    long v;
    if (!parsePct(value, v)) { err = "brightness must be a number 0..100"; return false; }
    settingsSetBrightness((uint8_t)v);
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
    // Reboot so the new name takes effect for the DHCP hostname, mDNS AND the OTA name together
    // (otaHostname()/wifiHostname() both derive from it at boot). netTask does the reset shortly
    // after this response is sent; the browser will need to reconnect at the new name.
    if (stateLock()) { g_pending.reboot = true; stateUnlock(); }
    return true;
  }

  if (field == "otaAuto") {
    // Auto-apply toggle. Accept 0/1 or false/true; anything else is a bad request, not "off".
    bool on;
    if (value == "1" || value == "true")       on = true;
    else if (value == "0" || value == "false") on = false;
    else { err = "otaAuto must be 0 or 1"; return false; }
    settingsSetOtaAuto(on);
    return true;
  }

  if (field == "updateUrl") {
    // The firmware manifest source, tri-state: "" = automatic (LAN portal if known, else the
    // GitHub-latest default), "off" = disable checking, or an explicit http(s) URL override.
    if (value.length() && value != "off" &&
        !value.startsWith("http://") && !value.startsWith("https://")) {
      err = "must be a http(s) URL, empty (automatic), or 'off'";
      return false;
    }
    settingsSetUpdateUrl(value);
    updaterForceCheck();   // re-check against the new source on netTask's next tick
    return true;
  }

  if (field == "updateNow") {
    // Explicit approve — the config page's "Update now" / the portal's Approve. Only meaningful
    // when an update is actually waiting; the apply happens on netTask (not this HTTP task) so the
    // response returns before the blocking flash begins.
    if (!updaterAvailable()) { err = "no update available"; return false; }
    updaterApprove();
    return true;
  }

  err = "unknown field";
  return false;
}

void webConfigTrackDeleted(const String &path) {
  if (settingsSleepTrack() == path) settingsSetSleepTrack("");
  if (settingsWakeTrack()  == path) settingsSetWakeTrack("");
}
