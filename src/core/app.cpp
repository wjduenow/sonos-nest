// Core application controller. Device-agnostic Sonos control extracted from main.cpp so
// every unit (UX) shares it. See plans/01-sonos-knob-controller-plan.md §3/§6/§7.
#include "app.h"

#include <Arduino.h>

#include "player_state.h"
#include "unit.h"                 // uiTick() — provided by whichever unit the build links
// Album art is the core's ONLY graphics coupling (LVGL + TJpg). A headless unit has nothing to
// show it on, so -DHEADLESS drops it — the include, the task, and its creation below. Nothing
// depends on artTask running: it only reads g_player.artUri and calls albumArt*.
#ifndef HEADLESS
#include "album_art.h"
#endif
#include "settings.h"
#include "library.h"
#include "net/wifi.h"
#include "net/ota.h"
#ifdef HEADLESS
#include "board.h"               // knobDown() — the button, as the deliberate re-provision trigger
#include "net/portal.h"          // portalRun() — SoftAP captive portal for headless provisioning
#endif
#include "sonos/ssdp.h"
#include "sonos/soap_client.h"

// Optional via include/secrets.h: SONOS_DEFAULT_ROOM "Name", CLOCK_TZ "<POSIX TZ>".
#if __has_include("secrets.h")
#include "secrets.h"
#endif

// POSIX timezone for the clock screensaver (DST handled automatically).
#ifndef CLOCK_TZ
#define CLOCK_TZ "PST8PDT,M3.2.0,M11.1.0"   // US Pacific
#endif

// --- FreeRTOS tasks (mutex-guarded shared PlayerState) ---
static void uiTask(void *arg);     // LVGL render + input (encoder, button, touch)
static void netTask(void *arg);    // drain pending commands + poll transport/position/volume
#ifndef HEADLESS
static void artTask(void *arg);    // on track change: fetch art -> TJpg decode -> cache
#endif

// Volume + now-playing-room are the selected speaker; transport must hit its group
// COORDINATOR (plan §3), which differs when the speaker is grouped.
static String s_zoneName;
static String s_zoneIp;     // the selected speaker (volume target)
static String s_coordIp;    // its group coordinator (transport/queue/now-playing target)
static String s_coordUuid;  // coordinator's RINCON uuid (for the x-rincon-queue URI)

// Switch the active zone to the speaker with this IP (from the ROOMS picker). Resets the
// volume/transport/coordinator targets and updates the shared state. Returns false if the
// IP isn't among the discovered zones.
static bool selectZoneByIp(const String &ip) {
  for (const auto &z : sonos::zones()) {
    if (z.ip != ip) continue;
    s_zoneName  = z.name;
    s_zoneIp    = z.ip;
    s_coordIp   = z.coordIp.length() ? z.coordIp : sonos::coordinatorIpFor(z.name);
    if (s_coordIp.length() == 0) s_coordIp = s_zoneIp;
    s_coordUuid = z.coordinatorUuid.length() ? z.coordinatorUuid : z.uuid;
    if (stateLock()) {
      g_player.zoneName        = z.name;
      g_player.coordinatorIp   = s_coordIp;
      g_player.coordinatorUuid = s_coordUuid;
      // Clear stale now-playing so the UI doesn't show the previous room's track.
      g_player.title.clear(); g_player.artist.clear(); g_player.album.clear();
      g_player.artUri.clear();
      stateUnlock();
    }
    settingsSetRoom(z.name);   // persist so the pick survives reboot
    Serial.printf("[zone] switched to %s @ %s (coord %s)\n",
                  s_zoneName.c_str(), s_zoneIp.c_str(), s_coordIp.c_str());
    return true;
  }
  return false;
}

// Pick the zone to control after discovery. With SONOS_DEFAULT_ROOM pinned (secrets.h),
// only settle once that exact room is discovered — otherwise return false so netTask keeps
// re-discovering (a single SSDP round can miss it). Without a pin, use the first zone.
static bool selectZone() {
  const std::vector<sonos::Zone> &zs = sonos::zones();
  if (zs.empty()) return false;
  // Preference order: saved room (NVS) -> SONOS_DEFAULT_ROOM -> first discovered.
  String want = settingsRoom();
#ifdef SONOS_DEFAULT_ROOM
  if (want.length() == 0) want = SONOS_DEFAULT_ROOM;
#endif
  size_t idx = 0;
  if (want.length()) {
    bool found = false;
    for (size_t i = 0; i < zs.size(); ++i)
      if (zs[i].name == want) { idx = i; found = true; break; }
    if (!found) {
      Serial.printf("[boot] '%s' not in %u discovered zones yet — retrying discovery\n",
                    want.c_str(), (unsigned)zs.size());
      return false;
    }
  }
  s_zoneName  = zs[idx].name;
  s_zoneIp    = zs[idx].ip;
  s_coordIp   = zs[idx].coordIp.length() ? zs[idx].coordIp : sonos::coordinatorIpFor(s_zoneName);
  if (s_coordIp.length() == 0) s_coordIp = s_zoneIp;
  s_coordUuid = zs[idx].coordinatorUuid.length() ? zs[idx].coordinatorUuid : zs[idx].uuid;
  if (stateLock()) {
    g_player.zoneName        = zs[idx].name;
    g_player.coordinatorIp   = s_coordIp;
    g_player.coordinatorUuid = s_coordUuid;
    stateUnlock();
  }
  Serial.printf("[boot] zone %s @ %s, coordinator @ %s\n",
                s_zoneName.c_str(), s_zoneIp.c_str(), s_coordIp.c_str());
  return true;
}

static uint32_t s_lastPoll = 0, s_lastVolCmd = 0, s_lastCoordRefresh = 0;

// Drain + execute queued input commands. Kept cheap and called frequently (including
// between the poll's SOAP calls) so a twist/press reaches the speaker with minimal lag.
static void processPending() {
  PendingCmds p;
  if (stateLock()) { p = g_pending; g_pending = PendingCmds(); stateUnlock(); }

  if (p.requestZoneIp.length()) {
    selectZoneByIp(p.requestZoneIp);
    s_lastPoll = 0;                      // poll the new zone immediately
    s_lastCoordRefresh = millis();
  }
  if (p.targetVolume >= 0) {
    sonos::setVolume(s_zoneIp, (uint8_t)p.targetVolume);   // volume -> the speaker
    s_lastVolCmd = millis();
  }
  // Explicit play/pause decided by the UI (no round-trip, correct action).
  if (p.setPlay == 0)      sonos::pause(s_coordIp);
  else if (p.setPlay == 1) sonos::play(s_coordIp);
  if (p.prev) sonos::previous(s_coordIp);   // transport -> the coordinator
  if (p.next) sonos::next(s_coordIp);

  // Grouping: join a speaker to the active group, or split one off. Re-discover topology
  // and refresh the active room's coordinator afterward, then signal the UI.
  if (p.groupJoinIp.length()) {
    sonos::setAvTransportUri(p.groupJoinIp, "x-rincon:" + s_coordUuid, "");
    sonos::ssdpDiscover();
    selectZoneByIp(s_zoneIp);
    g_zonesGen++;
  }
  if (p.groupLeaveIp.length()) {
    sonos::becomeStandalone(p.groupLeaveIp);
    sonos::ssdpDiscover();
    selectZoneByIp(s_zoneIp);
    g_zonesGen++;
  }
  // Play a locally-served file (the ES3C28P's HTTP media server) on the group coordinator,
  // looped. Enqueue it with minimal DIDL so Sonos accepts the http mp3, switch the transport
  // to the queue, set REPEAT_ALL, and play.
  if (p.localStreamUrl.length()) {
    String meta =
        "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
        "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">"
        "<item id=\"0\" parentID=\"-1\" restricted=\"1\">"
        "<dc:title>" + (p.localStreamTitle.length() ? p.localStreamTitle : String("Ocean Waves")) + "</dc:title>"
        "<upnp:class>object.item.audioItem.musicTrack</upnp:class>"
        "<res protocolInfo=\"http-get:*:audio/mpeg:*\">" + p.localStreamUrl + "</res>"
        "</item></DIDL-Lite>";
    sonos::removeAllTracksFromQueue(s_coordIp);           // clear the queue first
    if (p.targetVolume >= 0) sonos::setVolume(s_zoneIp, (uint8_t)p.targetVolume);  // volume before play
    sonos::addUriToQueue(s_coordIp, p.localStreamUrl, meta);
    sonos::setAvTransportUri(s_coordIp, "x-rincon-queue:" + s_coordUuid + "#0", "");
    sonos::setPlayMode(s_coordIp, "REPEAT_ALL");
    sonos::play(s_coordIp);
    s_lastPoll = millis() - 600;
  }

  // WiFi change requested from Settings: apply the new creds (blocking; reverts on failure).
  // Re-discover Sonos afterward since the network/IPs may have changed.
  if (p.wifiSsid.length()) {
    wifiApply(p.wifiSsid, p.wifiPass);
    if (wifiIsConnected()) { sonos::ssdpDiscover(); selectZone(); }
  }
  // Device-name change: reconnect so the router registers the new hostname.
  if (p.reboot) {
    // A device-name change: reboot so the DHCP hostname, mDNS and OTA name all come up fresh
    // from the new name. The web handler has already sent its HTTP response by now; the short
    // delay lets that TCP flush before the reset drops the link.
    Serial.println("[app] rebooting to apply new device name");
    delay(800);
    ESP.restart();
  }

  // After a transport change the track/state (and art) update — poll again soon, once the
  // speaker has settled out of TRANSITIONING, rather than waiting up to a full second.
  if (p.prev || p.next || p.setPlay >= 0) s_lastPoll = millis() - 600;

  // Browse / play requests (ContentDirectory off the UI thread).
  library::service(s_coordIp, s_coordIp, s_coordUuid);
}

static void uiTask(void *) {
  for (;;) {
    uiTick();                 // lv_timer_handler() + input handling
    // During an OTA, back off hard so the lower-priority loop task (which runs the OTA
    // write) isn't starved on this core — otherwise the transfer times out mid-upload.
    vTaskDelay(pdMS_TO_TICKS(otaActive() ? 120 : 5));
  }
}

static void netTask(void *) {
  for (;;) {
    if (otaActive()) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }  // yield bandwidth to OTA

    // If discovery failed at boot, keep retrying until we have a zone.
    if (s_zoneIp.length() == 0) {
      if (sonos::ssdpDiscover()) selectZone();
      if (s_zoneIp.length() == 0) { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
    }

    // Re-resolve the coordinator occasionally (grouping changes at runtime).
    if (millis() - s_lastCoordRefresh > 15000) {
      s_lastCoordRefresh = millis();
      String c = sonos::coordinatorIpFor(s_zoneName);
      if (c.length() && c != s_coordIp) {
        s_coordIp = c;
        if (stateLock()) { g_player.coordinatorIp = c; stateUnlock(); }
      }
    }

    processPending();

#ifdef HEADLESS
    // Headless (the button): no screen to keep fresh, so poll ONLY the transport state — just
    // enough for the press toggle to know whether Sonos is already playing — and do it slowly.
    // The full 1 Hz title/position/art/volume poll below exists for the screen units' Now Playing
    // display; none of that is shown here, and that constant traffic was pure overhead that also
    // drove the SOAP socket churn. One call every 3 s instead of ~3 every 1 s.
    if (millis() - s_lastPoll > 3000) {
      s_lastPoll = millis();
      TransportState st = TransportState::Unknown;
      if (sonos::getTransportInfo(s_coordIp, st)) {
        if (stateLock()) { g_player.transport = st; stateUnlock(); }
      }
    }
#else
    // Poll ~1 Hz, interleaving command processing so input never waits behind the full poll.
    if (millis() - s_lastPoll > 1000) {
      s_lastPoll = millis();
      TransportState st = TransportState::Unknown;
      PlayerState np;
      uint8_t vol = 0;
      bool gotVol = false;
      sonos::getTransportInfo(s_coordIp, st);  processPending();
      sonos::getPositionInfo(s_coordIp, np);   processPending();
      if (millis() - s_lastVolCmd > 1500) { gotVol = sonos::getVolume(s_zoneIp, vol); }

      if (stateLock()) {
        g_player.transport   = st;
        g_player.positionSec = np.positionSec;
        g_player.durationSec = np.durationSec;
        g_player.title       = np.title;
        g_player.artist      = np.artist;
        g_player.album       = np.album;
        g_player.artUri      = np.artUri;
        if (gotVol) g_player.volume = vol;
        g_player.dirty = true;
        stateUnlock();
      }
    }
#endif
    vTaskDelay(pdMS_TO_TICKS(15));
  }
}

#ifndef HEADLESS
static void artTask(void *) {
  String last;
  int    fails = 0;
  for (;;) {
    if (otaActive()) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }

    String cur;
    if (stateLock()) { cur = g_player.artUri; stateUnlock(); }
    if (cur != last) {
      if (cur.length() == 0) {
        albumArtClear();  last = cur;  fails = 0;
      } else if (albumArtFetch(cur)) {  // GET + TJpg decode + cache (never on UI task)
        last = cur;  fails = 0;
      } else if (++fails >= 4) {
        albumArtClear();  last = cur;  fails = 0;   // give up; don't leave the prior art up
      }
      // else: leave `last` unchanged so the next loop retries this URL (fixes stale art on
      // transient fetch failures during rapid track skipping).
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}
#endif  // !HEADLESS

void appBoot() {
#ifdef HEADLESS
  // Headless provisioning (sonos-button). Open the SoftAP captive portal ONLY when there are no
  // creds to try at all, or when the button is held through power-on (deliberate re-provision).
  // A failed connect with creds present must RETRY, never open the portal — else a brief router
  // outage would drop the box into AP mode and it would never rejoin (plans/04 Phase 5).
  if (knobDown() || !wifiHaveCreds()) {
    portalRun("sonos-button-setup");   // blocks until joined; tears the AP down on success
  } else if (!wifiConnect()) {
    for (int i = 0; i < 5 && !wifiIsConnected(); ++i) { delay(2000); wifiConnect(); }
  }
#else
  wifiConnect();          // NVS creds -> connect (screened units provision WiFi on-device)
#endif
  configTzTime(CLOCK_TZ, "pool.ntp.org", "time.nist.gov");  // clock screensaver time
  otaBegin();             // OTA listener — Phase 4
  sonos::ssdpDiscover();  // SSDP seed -> ZoneGroupTopology -> room list (§3)
  selectZone();
}

void appStartTasks() {
  // UI pinned to core 1; network work on core 0.
  xTaskCreatePinnedToCore(uiTask,  "ui",  8192, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 2, nullptr, 0);
#ifndef HEADLESS
  xTaskCreatePinnedToCore(artTask, "art", 8192, nullptr, 1, nullptr, 0);
#endif
}
