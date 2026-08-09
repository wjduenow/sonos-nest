// Core application controller. Device-agnostic Sonos control extracted from main.cpp so
// every unit (UX) shares it. See plans/01-sonos-knob-controller-plan.md §3/§6/§7.
#include "app.h"

#include <Arduino.h>
#include <WiFi.h>          // link-health check in the recovery path (see netTask)

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
#include "net/registrar.h"       // self-register with the sonos-portal dashboard (all units)
#include "net/updater.h"         // OTA pull path — check for/apply a published firmware update
#include "board.h"               // knobDown() — the deliberate re-provision trigger (held at boot)
#include "net/portal.h"          // portalRun() — SoftAP captive portal (all units, not just headless)
#include "sonos/ssdp.h"
#include "sonos/soap_client.h"
#include "sonos/didl.h"          // parseNowPlaying() — the CurrentURIMetaData title fallback
#include "sonos/gena.h"          // UPnP eventing — no-op unless -DGENA_EVENTS (see plans/09)
#include "room_status.h"         // per-room volume/transport for the Rooms screen (netTask-driven)

// Optional via include/secrets.h: SONOS_DEFAULT_ROOM "Name", CLOCK_TZ "<POSIX TZ>".
#include "net/logmirror.h"   // LOG — tees to the TCP mirror where enabled, plain Serial otherwise
// NB above the secrets conditional on purpose: inside it, LOG would only be defined on
// machines that happen to have include/secrets.h, which is gitignored.

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
    LOG.printf("[zone] switched to %s @ %s (coord %s)\n",
                  s_zoneName.c_str(), s_zoneIp.c_str(), s_coordIp.c_str());
    return true;
  }
  // Silent failure here is a trap: the UI has already given the user feedback (a tone, a screen
  // change) and nothing happens. Say which IP was asked for and what was actually known.
  LOG.printf("[zone] requested %s not found among %u known zone(s)\n", ip.c_str(),
                (unsigned)sonos::zones().size());
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
      LOG.printf("[boot] '%s' not in %u discovered zones yet — retrying discovery\n",
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
  LOG.printf("[boot] zone %s @ %s, coordinator @ %s\n",
                s_zoneName.c_str(), s_zoneIp.c_str(), s_coordIp.c_str());
  return true;
}

static uint32_t s_lastPoll = 0, s_lastVolCmd = 0, s_lastCoordRefresh = 0;

// Dead-link detection state (see netTask). File scope rather than a function-local static because
// the Wi-Fi supervisor at the top of the loop has to be able to clear it: an AP that actually goes
// away must not leave a half-armed streak behind that turns the NEXT sighting into a reboot.
static int      s_deadLinkStreak  = 0;
static uint32_t s_deadLinkFirstMs = 0;
static const uint32_t kDeadLinkWindowMs = 10000;

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

  // A ready-made URI + DIDL from the UI — a Radio station, or a favourite played from the cache.
  //
  // The scheme decides the mechanism, exactly as library::playItem() does: a CONTAINER (a service
  // playlist/album, or a Sonos saved queue) cannot be set as the transport URI, so it has to be
  // enqueued and the queue played. Everything else — stations, single tracks, streams — replaces
  // the transport directly. Verified on hardware that an x-sonosapi-radio: URI needs no
  // getMediaURI resolve step first (plans/08).
  if (p.playUri.length()) {
    if (p.playUri.startsWith("x-rincon-cpcontainer:") || p.playUri.startsWith("file:")) {
      sonos::removeAllTracksFromQueue(s_coordIp);
      sonos::addUriToQueue(s_coordIp, p.playUri, p.playMeta);
      sonos::setAvTransportUri(s_coordIp, "x-rincon-queue:" + s_coordUuid + "#0", "");
    } else {
      sonos::setAvTransportUri(s_coordIp, p.playUri, p.playMeta);
    }
    sonos::play(s_coordIp);
    s_lastPoll = 0;                        // reflect the new track immediately
  }

  // Grouping. Every op in the batch is applied first, and the topology is re-read ONCE at the
  // end: ssdpDiscover() is a full GetZoneGroupState fetch + parse (tens of KB of XML in a big
  // house), so doing it per room made ungrouping a four-room group four times slower than it
  // needed to be, for a result only the last pass could be correct about anyway.
  if (p.ungroupAll) {
    // Split off every OTHER member of the active group; the active room stays put and becomes a
    // group of one. Leaving it grouped to a coordinator we just dissolved is what "Ungroup"
    // must not do, and standalone-ing it too would needlessly interrupt its own playback.
    std::vector<sonos::Zone> zs;
    sonos::zonesSnapshot(zs);
    for (const auto &z : zs) {
      if (z.coordinatorUuid != s_coordUuid) continue;   // not in our group
      if (z.ip == s_zoneIp) continue;                   // the active room keeps playing
      sonos::becomeStandalone(z.ip);
    }
  }
  for (const auto &op : p.groupOps) {
    if (!op.ip.length()) continue;
    if (op.join) sonos::setAvTransportUri(op.ip, "x-rincon:" + s_coordUuid, "");
    else         sonos::becomeStandalone(op.ip);
  }
  if (p.ungroupAll || !p.groupOps.empty()) {
    sonos::ssdpDiscover();
    selectZoneByIp(s_zoneIp);
    g_zonesGen++;
    // Deliberately NOT dropping the per-room status cache here: roomstatus carries volume and
    // still-valid transport across the topology change itself. Blanking it made every checkbox
    // tap wipe the page back to "--" for a second and a half.
  }

  // Per-room controls from the Rooms page.
  if (p.roomVolIp.length() && p.roomVolTarget >= 0) {
    sonos::setVolume(p.roomVolIp, (uint8_t)constrain(p.roomVolTarget, 0, 100));
  }
  if (p.roomPlayCoordIp.length() && p.roomSetPlay >= 0) {
    if (p.roomSetPlay) sonos::play(p.roomPlayCoordIp);
    else               sonos::pause(p.roomPlayCoordIp);
    // If that was the room we are following, reflect it on Now Playing without waiting a second.
    if (p.roomPlayCoordIp == s_coordIp) s_lastPoll = 0;
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
    LOG.println("[app] rebooting to apply new device name");
    delay(800);
    ESP.restart();
  }

  // After a transport change the track/state (and art) update — poll again soon, once the
  // speaker has settled out of TRANSITIONING, rather than waiting up to a full second.
  if (p.prev || p.next || p.setPlay >= 0) s_lastPoll = millis() - 600;

  // Browse / play requests (ContentDirectory off the UI thread).
  library::service(s_coordIp, s_coordIp, s_coordUuid);
}

// Consecutive coordinator-poll failures. A DHCP renewal (router reboot, lease expiry) over a
// multi-day uptime moves the Sonos speaker to a new IP; nothing re-runs SSDP once a zone is picked,
// so coordinatorIpFor() just keeps handing back the stale cached IP and every command fails
// silently. When the transport poll fails a few times running, flag the coordinator stale — the
// netTask loop's single discovery site re-discovers BEFORE it drains any queued command, so a
// voice/button command that lands during the outage dispatches to the fresh IP instead of being
// consumed (processPending() clears g_pending whether or not the command reaches the speaker)
// against the dead one. We only raise the flag here: the blocking ssdpDiscover() must NOT run
// inline in the poll path, where it would stall processPending() for its whole multicast wait
// (up to ~3.6 s), delaying every queued command behind it.
static int  s_pollFails  = 0;
static bool s_coordStale = false;
static void notePollResult(bool ok) {
  if (ok) { s_pollFails = 0; return; }
  if (++s_pollFails < 3) return;
  s_pollFails = 0;
  s_coordStale = true;
}

static void uiTask(void *) {
  for (;;) {
    uiTick();                 // lv_timer_handler() + input handling
    // During an OTA, back off hard: for espota so the loop task's write isn't starved, and for a
    // pull-flash (updaterActive) so this core stops executing flash code while the writer erases
    // the OTA slot — a cache-disable fault there resets the device mid-download.
    vTaskDelay(pdMS_TO_TICKS((otaActive() || updaterActive()) ? 120 : 5));
  }
}

// Link snapshot for the UI. See the contract in app.h: these exist so uiTick() never has to make
// a blocking radio RPC to log link health. Refreshed on a timer rather than every loop iteration
// because on ESP-Hosted boards each read is an SDIO round-trip to the C6, and this is diagnostics,
// not control.
volatile int      g_linkStatus = 0;
volatile int      g_linkRssi   = 0;
volatile uint32_t g_linkIp     = 0;
volatile uint32_t g_linkZones  = 0;

static void publishLinkStats() {
  static uint32_t s_last = 0;
  if (s_last && millis() - s_last < 2000) return;
  s_last = millis();
  g_linkStatus = (int)WiFi.status();
  g_linkRssi   = (int)WiFi.RSSI();
  const IPAddress ip = WiFi.localIP();
  g_linkIp = (uint32_t)ip[0] | ((uint32_t)ip[1] << 8) | ((uint32_t)ip[2] << 16) |
             ((uint32_t)ip[3] << 24);
  g_linkZones = (uint32_t)sonos::zones().size();
}

static void netTask(void *) {
  for (;;) {
    if (otaActive()) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }  // yield bandwidth to OTA
    publishLinkStats();

    // Wi-Fi supervisor: a transient outage (router reboot, DHCP renewal, AP roam) is near-certain
    // over a multi-day uptime and must self-heal, or the unit sits alive-but-wedged until a power
    // cycle — the "unresponsive after 2-3 days" report. The framework's implicit auto-reconnect is
    // unreliable after some disconnect reasons, so re-kick the connect ourselves, backed off so we
    // don't spin. Everything below (discovery, SOAP, registrar) is pointless without a link.
    if (!wifiIsConnected()) {
      static uint32_t s_lastWifiKick = 0;
      const uint32_t now = millis();
      if (now - s_lastWifiKick > 10000) {
        s_lastWifiKick = now;
        LOG.println("[net] wifi down — reconnecting");
        wifiReconnect();
      }
      // A genuine disconnect is NOT the fault netLinkRecover() exists for, and the RSSI-0 readings
      // around a reconnect are normal. Disarm, or an ordinary router reboot ends in a device
      // reboot — exactly what the "requires the symptom twice" rule was written to prevent.
      if (s_deadLinkStreak) {
        LOG.println("[net] wifi genuinely down — clearing the dead-link streak");
        s_deadLinkStreak = 0;
      }
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    // Single discovery site. Runs at boot (no zone picked yet) and on recovery (notePollResult
    // flagged the coordinator unreachable — a moved IP). ssdpDiscover() is a blocking multicast +
    // SOAP wait, so it lives here at a defined loop point, never buried inline in the poll. On
    // recovery, re-resolve the coordinator and THEN drain g_pending, so a command queued during the
    // outage hits the fresh IP rather than being consumed against the dead one; re-poll immediately
    // to confirm reachability.
    if (s_zoneIp.length() == 0 || s_coordStale) {
      const bool recovering = s_coordStale;
      s_coordStale = false;
      if (recovering) LOG.println("[net] coordinator unreachable x3 — re-discovering Sonos");

      // Before blaming Sonos, check whether OUR link is the thing that died. A board whose radio
      // is a separate co-processor can report WL_CONNECTED with a live IP while the transport to
      // that co-processor is gone — RSSI comes from the co-processor, so 0 while "connected" means
      // the RPC is dead, not that the signal is weak. Re-discovery cannot succeed in that state,
      // and reconnecting Wi-Fi cannot fix it either: the reconnect path talks over the same dead
      // transport. Ask the board to rebuild the link, then re-associate.
      // Require the symptom TWICE before acting. Recovery on this board is a reboot, and a
      // single transient must never cost the user a reboot — RSSI can read 0 momentarily around
      // a roam or a scan without the transport being dead.
      // Both sightings must fall inside kDeadLinkWindowMs. Without a window the counter is
      // effectively permanent: one transient RSSI 0 now and another an hour later would add up to
      // a reboot, and the two would have nothing to do with each other.
      if (s_deadLinkStreak && millis() - s_deadLinkFirstMs > kDeadLinkWindowMs) {
        LOG.println("[net] dead-link streak expired — starting over");
        s_deadLinkStreak = 0;
      }
      if (recovering && WiFi.status() == WL_CONNECTED && WiFi.RSSI() == 0) {
        if (s_deadLinkStreak == 0) s_deadLinkFirstMs = millis();
        if (++s_deadLinkStreak >= 2) {
          LOG.println("[net] RSSI 0 while 'connected' twice — the radio link is dead");
          if (netLinkRecover()) {   // may not return: see the board implementation
            wifiConnect();
            LOG.printf("[net] link rebuilt: wifi=%d rssi=%d ip=%s\n", (int)WiFi.status(),
                          (int)WiFi.RSSI(), WiFi.localIP().toString().c_str());
          }
          s_deadLinkStreak = 0;
        } else {
          // Confirm the second sighting in SECONDS, not minutes. Falling through to the discovery
          // below would burn ~90 s on multicast that cannot possibly be answered over a dead
          // radio, and re-entering this branch would then cost another 3 failed polls — which is
          // why an obviously-dead link used to take ~3 minutes of empty room list to recover.
          // Skip the doomed discovery, re-arm the recovery flag, and look again shortly.
          LOG.println("[net] RSSI 0 while 'connected' — re-checking in 3s (needs 2 in a row)");
          s_coordStale = true;
          vTaskDelay(pdMS_TO_TICKS(3000));
          continue;
        }
      } else if (WiFi.RSSI() != 0) {
        s_deadLinkStreak = 0;
      }
      if (sonos::ssdpDiscover()) selectZone();
      if (s_zoneIp.length() == 0) { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
      if (recovering) { processPending(); s_lastPoll = 0; continue; }
    }

    // Re-resolve the coordinator occasionally (grouping changes at runtime). 60 s, not 15: this is
    // a full GetZoneGroupState fetch + parse of the whole topology XML (tens of KB in a big house),
    // String-heavy every time — and a stale IP is now caught within seconds by notePollResult()
    // anyway, so the frequent refresh bought little but heap churn.
    if (millis() - s_lastCoordRefresh > 60000) {
      s_lastCoordRefresh = millis();
      String c = sonos::coordinatorIpFor(s_zoneName);
      if (c.length() && c != s_coordIp) {
        s_coordIp = c;
        if (stateLock()) { g_player.coordinatorIp = c; stateUnlock(); }
        // Subscriptions are per-coordinator, so they have to follow it. This is the one place the
        // coordinator changes at runtime (grouping), and genaTick() does the actual resubscribe.
        sonos::genaSetCoordinator(c);
      }
    }

    processPending();
    registrarTick();   // heartbeat to the portal (self-rate-limited to ~45 s; retries discovery)
    updaterTick();     // OTA pull check (self-rate-limited ~6 h; applies only on explicit approve)
    // Per-room status for the Rooms screen. Costs nothing unless that page is actually open, and
    // polls at most one room per pass — see room_status.h for why it must not burst.
    roomstatus::tick();
    // GENA subscribe/renew. No-op without -DGENA_EVENTS; self-rate-limited, one blocking call per
    // pass at most.
    sonos::genaTick();

#ifdef HEADLESS
    // Headless (the button): no screen to keep fresh, so poll ONLY the transport state — just
    // enough for the press toggle to know whether Sonos is already playing — and do it slowly.
    // The full 1 Hz title/position/art/volume poll below exists for the screen units' Now Playing
    // display; none of that is shown here, and that constant traffic was pure overhead that also
    // drove the SOAP socket churn. One call every 3 s instead of ~3 every 1 s.
    if (millis() - s_lastPoll > 3000) {
      s_lastPoll = millis();
      TransportState st = TransportState::Unknown;
      const bool ok = sonos::getTransportInfo(s_coordIp, st);
      // When playing, also learn the SOURCE (GetMediaInfo/CurrentURI) so a press can tell "already
      // playing our queue" (stop it, even if we didn't start it or just rebooted) from TV/line-in
      // (override -> start the playlist). Only while playing: keeps the button at ~one extra call
      // and only when it matters. Cleared otherwise so a stale URI can't be read as "still playing".
      String uri;
      if (ok && st == TransportState::Playing) sonos::getMediaInfo(s_coordIp, uri);
      if (ok && stateLock()) { g_player.transport = st; g_player.currentUri = uri; stateUnlock(); }
      notePollResult(ok);   // re-discover if the coordinator has gone unreachable (moved IP)
    }
#else
    // --- Poll cadence ---------------------------------------------------------------------------
    //
    // 1 Hz normally — what every unit without eventing always does. 15 s while GENA is carrying
    // the state, which is a 15x traffic cut on its own (measured 3.00 SOAP calls/sec -> ~0.09).
    //
    // ONLY the RATE changes. The poll body below is identical either way, on purpose; see the
    // comment there for the bug that came from trimming it.
    //
    // TRUST IS REVOCABLE. It needs a live subscription AND at least one event received — a
    // SUBSCRIBE that Sonos accepts but whose callback it cannot reach would otherwise silently
    // downgrade the screen to 15 s updates. The moment either stops holding, this returns to 1 Hz
    // by itself, so a lapsed subscription or a failed renewal costs latency, never correctness.
    //
    // Note "at least one event" is weak on its own: Sonos always sends an initial state dump at
    // SUBSCRIBE time, so the counter is non-zero within a second of boot whether or not any LATER
    // event will ever arrive. That is tolerable precisely BECAUSE the backstop is complete — the
    // worst case of misplaced trust is a 15 s-stale screen, not a blank one.
    bool genaTrusted = false;
#ifdef GENA_EVENTS
    {
      sonos::GenaDiag gd;
      sonos::genaDiag(gd);
      genaTrusted = gd.subscribed && gd.events > 0;
    }
#endif
    const uint32_t pollEveryMs = genaTrusted ? 15000 : 1000;

    // ONE poll body, two cadences. The backstop is the SAME poll, just slower — it is emphatically
    // not a reduced one.
    //
    // It was a reduced one, and that was a real bug: it fetched position and threw the track
    // metadata away, on the theory that events owned those fields. So Now Playing became entirely
    // event-dependent, and starting playback showed "Nothing playing" indefinitely — the forced
    // immediate poll discarded the very title it had just fetched, and only an AVTransport event
    // could fill it in. Anything that dropped or delayed one event stranded the screen, and a zone
    // switch (which clears the track fields) stranded it too.
    //
    // The premise was always "events make it feel instant, the poll guarantees correctness". A
    // backstop that cannot reconstruct the whole screen on its own does not guarantee anything.
    // The in-flight-event race that motivated the reduction is not real either: a SOAP response is
    // live data at the moment it lands, so writing it is never a step backwards.
    if (millis() - s_lastPoll > pollEveryMs) {
      s_lastPoll = millis();
      PlayerState np;
      TransportState st = TransportState::Unknown;
      uint8_t vol = 0;
      bool gotVol = false;

      const bool ok = sonos::getTransportInfo(s_coordIp, st);  processPending();
      sonos::getPositionInfo(s_coordIp, np);                   processPending();

      // Fallback for content playing OUTSIDE the queue. A direct Spotify track's TrackMetaData is
      // a stub — item id="-1", no dc:title, no dc:creator, no art — so the screen read "Nothing
      // playing" while the speaker was audibly fine. The title is on the speaker, just in
      // CurrentURIMetaData instead (verified: empty TrackMetaData alongside dc:title "Apologize").
      // Costs an extra SOAP call ONLY when the primary source produced nothing, so normal content
      // pays nothing at all.
      if (np.title.length() == 0 && st != TransportState::Stopped) {
        String uri, meta;
        if (sonos::getMediaInfo(s_coordIp, uri, &meta) && meta.length() > 20) {
          PlayerState alt;
          sonos::parseNowPlaying(meta, alt);
          if (alt.title.length() || alt.artist.length()) {
            np.title = alt.title; np.artist = alt.artist;
            np.album = alt.album; np.artUri = alt.artUri;
          }
        }
        processPending();
      }

      if (millis() - s_lastVolCmd > 1500) { gotVol = sonos::getVolume(s_zoneIp, vol); }

      if (stateLock()) {
        g_player.transport    = st;
        g_player.positionSec  = np.positionSec;
        g_player.positionAtMs = millis();
        g_player.durationSec  = np.durationSec;
        g_player.title        = np.title;
        g_player.artist       = np.artist;
        g_player.album        = np.album;
        g_player.artUri       = np.artUri;
        if (gotVol) g_player.volume = vol;
        g_player.dirty = true;
        stateUnlock();
      }
      notePollResult(ok);   // re-discover if the coordinator has gone unreachable (moved IP)
    }
#endif
    vTaskDelay(pdMS_TO_TICKS(15));
  }
}

#ifndef HEADLESS
// Wait for the art URI to stop changing before downloading it. A cover is a real HTTP transfer —
// 228 KB was measured on a live system — so every track change costs one. Anything that walks
// through tracks or rooms quickly (skipping, or tapping down a room list) would otherwise queue a
// full download per step and push megabytes in a few seconds. On the ESP32-P4 that traffic goes
// over the SDIO bridge to the Wi-Fi co-processor, where sustained load is what provokes the
// transport failure netLinkRecover() exists to recover from; on the S3 units it is simply wasted
// bandwidth and heap churn. Settling first means a burst of N changes costs ONE fetch, of the
// track you actually landed on.
static const uint32_t kArtSettleMs = 700;

static void artTask(void *) {
  String last, pending;
  uint32_t pendingSince = 0;
  int    fails = 0;
  for (;;) {
    if (otaActive() || updaterActive()) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }

    String cur;
    if (stateLock()) { cur = g_player.artUri; stateUnlock(); }

    // Restart the settle timer every time the target moves.
    if (cur != pending) { pending = cur; pendingSince = millis(); }

    if (cur != last) {
      if (cur.length() == 0) {
        // Clearing is free and must be immediate, or the previous room's cover lingers.
        albumArtClear();  last = cur;  fails = 0;
      } else if (millis() - pendingSince < kArtSettleMs) {
        // Still moving — do not start a download we are about to throw away.
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
  // First-boot / re-provision WiFi — uniform across every unit. Open the SoftAP captive portal
  // when there are NO creds to try at all, or the knob/button is held through power-on (a
  // deliberate re-provision; knobDown() is false on boards without one). A merely-failed connect
  // must RETRY, never open the portal — else a brief router outage would drop a configured device
  // into AP mode and it would never rejoin (plans/04 Phase 5). Screened units draw a "join <AP>"
  // message via uiProvisioning() (the UI task isn't running yet); headless units no-op it.
  if (!wifiHaveCreds() || knobDown()) {
    String ap = wifiHostname() + "-setup";   // e.g. sonos-nest-setup / sonos-sleep-setup
    uiProvisioning(ap.c_str());
    portalRun(ap.c_str());                   // blocks until joined; tears the AP down on success
  } else if (!wifiConnect()) {
    for (int i = 0; i < 5 && !wifiIsConnected(); ++i) { delay(2000); wifiConnect(); }
  }
  configTzTime(CLOCK_TZ, "pool.ntp.org", "time.nist.gov");  // clock screensaver time
  otaBegin();             // OTA listener — Phase 4
  sonos::ssdpDiscover();  // SSDP seed -> ZoneGroupTopology -> room list (§3)
  selectZone();
  // Self-register with the LAN portal. After otaBegin() (ArduinoOTA has started mDNS, so the
  // portal service is resolvable) AND after selectZone() (so the payload's zone list is populated
  // on the very first registration). Best-effort; registrarTick() retries if the portal is down.
  registrarBegin();

  // OTA pull check. Dormant unless settingsUpdateUrl() is set; if otaAuto is on and a newer build
  // is published for this unit, this flashes + reboots HERE — before playback starts — rather than
  // mid-run. Runtime checks (explicit approve / portal-approved) happen in netTask via updaterTick.
  updaterBegin();

  // GENA callback listener. No-op unless -DGENA_EVENTS (jukebox only for now — see plans/09 for
  // the per-unit sizing; the sleep-machine cannot afford it). Started after selectZone() so the
  // coordinator below is the real one, and the listener task waits for Wi-Fi itself anyway.
  sonos::genaBegin();
  sonos::genaSetCoordinator(s_coordIp);
}

void appStartTasks() {
  // UI pinned to core 1; network work on core 0.
  xTaskCreatePinnedToCore(uiTask,  "ui",  8192, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 2, nullptr, 0);
#ifndef HEADLESS
  xTaskCreatePinnedToCore(artTask, "art", 8192, nullptr, 1, nullptr, 0);
#endif
}
