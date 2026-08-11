// Remote-config surface — see webconfig.h. Runs on whatever task the board's HTTP server uses,
// so every write goes through the same guarded paths the UI task uses: NVS for the persisted
// picks, and g_pending (under stateLock) for anything netTask has to act on.
#include "webconfig.h"
#include "settings.h"
#include "player_state.h"   // g_pending + stateLock/stateUnlock — the cross-task command channel
#include "board.h"          // localTrack* — the HAL's view of local storage (empty on most boards)
#include "sonos/ssdp.h"
#include "sonos/soap_client.h"   // soapDiag() — runtime SOAP counters for the health readout
#include "sonos/gena.h"          // genaDiag() — eventing counters; stubbed out without GENA_EVENTS
#include "heap_watch.h"          // heapwatch::worst() — which subsystem owns the heap low-water
#ifndef HEADLESS
#include "ui/album_art.h"        // albumArtDiag() — art fetch/fail/clear counters
#endif
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

// Bumped only by a playlist-pick edit — see webconfig.h.
static uint32_t s_playlistGen = 0;
uint32_t webConfigPlaylistGen() { return s_playlistGen; }

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
// The (unsigned char) cast is required rather than cosmetic — isdigit() is undefined for a
// negative argument and plain char is signed on this toolchain, so any byte >= 0x80 sign-extends.
static bool parsePct(const String &value, long &out) {
  if (value.length() == 0) return false;
  for (unsigned i = 0; i < value.length(); ++i) if (!isdigit((unsigned char)value[i])) return false;
  out = value.toInt();
  return out >= 0 && out <= 100;
}

// "playlist"/"volume" -> press slot 1 (single press), "…2" -> double, "…3" -> triple. The
// un-suffixed spelling is slot 1 rather than a fourth name so the field the config page has always
// posted keeps meaning what it did.
static uint8_t pressSlot(const String &field) {
  if (field.endsWith("2")) return 2;
  if (field.endsWith("3")) return 3;
  return 1;
}

String webConfigJson() {
  JsonDocument doc;
  doc["sleepTrack"] = settingsSleepTrack();
  doc["wakeTrack"]  = settingsWakeTrack();
  doc["room"]       = settingsRoom();
  doc["ring"]       = settingsRing();
  doc["brightness"] = settingsBrightness();   // screen units (nest); the unit applies changes
  // The button's three press slots. playlist/volume stay un-suffixed for slot 1 (single press) so
  // the field names the config page has always posted keep working; 2 and 3 are double and triple.
  doc["playlist"]   = settingsPlaylist(1);
  doc["volume"]     = settingsVolume(1);
  doc["playlist2"]  = settingsPlaylist(2);   // "" = unmapped: that press does nothing
  doc["volume2"]    = settingsVolume(2);
  doc["playlist3"]  = settingsPlaylist(3);
  doc["volume3"]    = settingsVolume(3);
  // Radio catalogue refresh. Belongs in the CONFIG document, not the registration payload — that
  // one is identity only (who and where), and putting settings there means the config page cannot
  // read back what it just wrote.
  // On-device sound. uiSound is the master level (0 = off); scrollSound gates the carousel
  // detents only and is meaningless while the master is 0 — the page reflects that by disabling it.
  doc["uiSound"]     = settingsUiSound();
  doc["scrollSound"] = settingsScrollSound();
  // Screensaver. Four fields rather than one because they are four separate decisions — see
  // settings.h. Reported on every unit; boards without a screen simply never read them back.
  doc["saver_mode"]      = settingsSaverMode();
  doc["saver_delay_sec"] = settingsSaverDelaySec();
  doc["saver_dim"]       = settingsSaverDimPct();
  doc["saver_blank_min"] = settingsSaverBlankMin();
  doc["radio_refresh_hour"] = settingsRadioRefreshHour();
  doc["radio_auto_refresh"] = settingsRadioAutoRefresh();
  doc["fav_refresh_hour"]   = settingsFavRefreshHour();
  doc["fav_auto_refresh"]   = settingsFavAutoRefresh();
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
  // Largest contiguous internal block. Watch this alongside heapFree, not instead of it: the way
  // internal SRAM actually kills this project is FRAGMENTATION, not exhaustion. On the nest, ~15 KB
  // free with a 7.6 KB largest block left LWIP unable to get socket buffers, and the symptom was
  // Sonos "connection refused" — heapFree alone never showed the fault coming.
  h["heapLargest"] = (uint32_t)ESP.getMaxAllocHeap();
  // PSRAM. Wide open on every unit here (the jukebox uses ~2.4 MB of 32 MB), which is exactly why
  // it belongs in the readout: it's where new work SHOULD land, and this is how you confirm a new
  // buffer went there rather than quietly onto the internal heap.
  h["psramSize"] = (uint32_t)ESP.getPsramSize();
  h["psramFree"] = (uint32_t)ESP.getFreePsram();
  // psramMin is EMITTED ONLY WHEN IT IS REAL. heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)
  // returns a flat 0 on the P4 — IDF doesn't track a low-water for that heap there — and a
  // published 0 reads as "PSRAM was fully exhausted at some point", the opposite of the truth (an
  // actual 0 would have failed allocations). Absent means "not tracked on this target"; present
  // means trustworthy. Verified non-zero paths keep the field; don't "fix" this by always emitting.
  const uint32_t psMin = (uint32_t)ESP.getMinFreePsram();
  if (psMin) h["psramMin"] = psMin;
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

  // WHERE the internal-heap low-water actually happened (core/heap_watch.h). heapMin says how
  // close the device came to the ~15 KB cliff where LWIP starves; this says what was holding the
  // memory at that moment, which is the part you cannot get any other way — the dips are
  // transient, rare, and invisible by the time anyone looks.
  {
    heapwatch::Low lw;
    heapwatch::worst(lw);
    if (lw.tag && lw.tag[0]) {
      JsonObject w = h["heapLow"].to<JsonObject>();
      w["tag"]     = lw.tag;                  // e.g. gena.unescape, favs.page, art.fetch, poll
      w["free"]    = lw.freeBytes;
      w["largest"] = lw.largestFree;
      w["atSec"]   = lw.atMs / 1000;
    }
  }

  // What the device BELIEVES is playing. Added after a bug where Now Playing sat on "Nothing
  // playing" while the speaker was fine: with no way to read g_player remotely, telling "the
  // device has the wrong state" apart from "the screen is not drawing it" needed a person in front
  // of the panel. This is the cheap way to answer that from a terminal.
  {
    JsonObject n = h["nowPlaying"].to<JsonObject>();
    if (stateLock()) {
      n["transport"] = (int)g_player.transport;   // 0=Stopped 1=Playing 2=Paused 3=Transitioning 4=Unknown
      n["title"]     = g_player.title;
      n["artist"]    = g_player.artist;
      n["posSec"]    = g_player.positionSec;
      n["durSec"]    = g_player.durationSec;
      n["volume"]    = g_player.volume;
      n["room"]      = g_player.zoneName;
      n["hasArt"]    = g_player.artUri.length() > 0;
      // Art pipeline counters. clears climbing while a track is loaded is the signature of the
      // "artwork flickers" report — the UI hides the cover ONLY when albumArtClear() has run.
#ifndef HEADLESS
      AlbumArtDiag ad; albumArtDiag(ad);
      n["artFetch"]  = ad.fetches;
      n["artFail"]   = ad.failures;
      n["artClear"]  = ad.clears;
#endif
      // The TAIL of the URL, not a bool. "art disappears and comes back" can be artUri toggling
      // between two different URLs for the same track, or going empty — a boolean cannot tell
      // those apart, and that ambiguity has already sent one fix in the wrong direction.
      n["artTail"]   = g_player.artUri.length() > 40
                         ? g_player.artUri.substring(g_player.artUri.length() - 40)
                         : g_player.artUri;
      stateUnlock();
    }
  }

  // GENA eventing (plans/09, issue #6). Emitted only on units that have it compiled in — port is
  // 0 when the no-op stubs are linked, so its ABSENCE means "not built with -DGENA_EVENTS" rather
  // than "built and broken". That distinction is the whole point of the block: eventing failing
  // silently looks exactly like eventing working, because the backstop poll keeps the screen
  // correct either way. Read it as: subscribed=false or a climbing resubscribes/failures means the
  // poll is quietly carrying the device; a lastEventAgeMs that only ever grows means subscriptions
  // are live but nothing is arriving (callback unreachable — check the speaker can reach US).
  sonos::GenaDiag gd;
  sonos::genaDiag(gd);
  if (gd.port) {
    JsonObject g = h["gena"].to<JsonObject>();
    g["port"]         = gd.port;
    g["subscribed"]   = gd.subscribed;
    g["events"]       = gd.events;
    g["renewals"]     = gd.renewals;
    g["resubscribes"] = gd.resubscribes;
    g["failures"]     = gd.failures;
    if (gd.lastEventAgeMs != UINT32_MAX) g["lastEventAgeMs"] = gd.lastEventAgeMs;
  }

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
#elif defined(UNIT_JUKEBOX)
  doc["unit"] = "jukebox"; doc["board"] = "crowpanel_p4_7in";
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

  if (field == "uiSound") {
    long v = 0;                                   // parsePct takes a long&, like the other levels
    if (!parsePct(value, v)) { err = "uiSound must be a number 0..100"; return false; }
    settingsSetUiSound((uint8_t)v);
    return true;
  }
  if (field == "scrollSound") {
    settingsSetScrollSound(value == "1" || value == "true" || value == "on");
    return true;
  }
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

  // Favourites keep a schedule of their own — see settings.h for why they no longer share the
  // radio one.
  if (field == "fav_refresh_hour") {
    const int h = value.toInt();
    if (h < 0 || h > 23) { err = "hour must be 0-23"; return false; }
    // Warn rather than refuse: two heavy refreshes in the same hour is real memory pressure on the
    // P4, but it is the owner's call and a same-hour setting is not invalid.
    if (h == (int)settingsRadioRefreshHour())
      err = "note: same hour as the station refresh — they will compete for memory";
    settingsSetFavRefreshHour((uint8_t)h);
    return true;
  }

  if (field == "fav_auto_refresh") {
    settingsSetFavAutoRefresh(value == "1" || value == "true" || value == "on");
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

  if (field == "ring" || field == "volume" || field == "volume2" || field == "volume3") {
    // toInt() yields 0 on garbage, and 0 is legitimate for both of these, so validate the text
    // rather than trusting the parse — otherwise "banana" silently means "off"/"silent".
    long v;
    if (!parsePct(value, v)) { err = field + " must be a number 0..100"; return false; }
    if (field == "ring") settingsSetRing((uint8_t)v);   // the unit applies it via webConfigGen
    else settingsSetVolume(pressSlot(field), (uint8_t)v);  // read at press time; nothing to apply
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

  // --- Screensaver ------------------------------------------------------------------------------
  // All four bump s_gen: the unit owns the idle state machine and the backlight, so it has to be
  // told a setting moved rather than re-reading NVS every tick.
  if (field == "saver_mode") {
    // EXACTLY one digit, not "parses as a number in range". String::toInt() stops at the first
    // non-numeric character and returns the prefix, so "2junk" would silently persist mode 2 —
    // accepting input nobody meant to send, which is how a config API drifts from its own docs.
    if (value.length() != 1 || value[0] < '0' || value[0] > ('0' + SAVER_AUTO)) {
      err = "saver_mode must be 0 (off), 1 (clock), 2 (cover art) or 3 (auto)";
      return false;
    }
    settingsSetSaverMode((uint8_t)(value[0] - '0'));
    s_gen++;
    return true;
  }

  if (field == "saver_delay_sec" || field == "saver_blank_min") {
    // Digits only: toInt() returns 0 on garbage and 0 is the legitimate "never" value for both, so
    // "banana" would silently mean "never show a screensaver" and look like the feature is broken.
    // The (unsigned char) cast is required, not tidiness: isdigit() is undefined for a negative
    // argument, and plain char is signed here, so any byte >= 0x80 sign-extends.
    if (value.length() == 0) { err = field + " must be a number"; return false; }
    for (unsigned i = 0; i < value.length(); ++i) {
      if (!isdigit((unsigned char)value[i])) { err = field + " must be a number"; return false; }
    }
    const long v = value.toInt();
    if (v > 65535) { err = field + " is too large"; return false; }
    if (field == "saver_delay_sec") settingsSetSaverDelaySec((uint16_t)v);
    else                            settingsSetSaverBlankMin((uint16_t)v);
    s_gen++;
    return true;
  }

  if (field == "saver_dim") {
    long v;
    if (!parsePct(value, v)) { err = "saver_dim must be a number 0..100"; return false; }
    settingsSetSaverDimPct((uint8_t)v);   // floors at 5; see settings.h
    s_gen++;
    return true;
  }

  if (field == "playlist" || field == "playlist2" || field == "playlist3") {
    const uint8_t slot = pressSlot(field);
    // Slot 1 is the button's whole reason to exist and must always point at something; 2 and 3 are
    // opt-in, so "" is a legitimate value there — it's how you un-map a double/triple press.
    if (value.length() == 0) {
      if (slot == 1) { err = "pick a playlist"; return false; }
      settingsSetPlaylist(slot, "");
      s_gen++; s_playlistGen++;
      return true;
    }
    // Validate against the browsed list when we have one: a typo'd name fails at 2am with the
    // button doing nothing, which is the worst possible time to discover it. Before the first
    // browse lands the cache is empty — accept then, rather than refusing to configure at all.
    if (!s_playlists.empty()) {
      bool known = false;
      for (const String &n : s_playlists) if (n == value) { known = true; break; }
      if (!known) { err = "no such playlist"; return false; }
    }
    settingsSetPlaylist(slot, value);
    s_gen++;           // signal the unit to re-warm its play-by-name cache for the new pick
    s_playlistGen++;   // ...and that this one must re-RESOLVE, not accept a cache hit
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
