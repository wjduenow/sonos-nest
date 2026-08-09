// See registrar.h. Outbound self-registration with the sonos-portal dashboard.
#include "registrar.h"
#include "../settings.h"     // settingsPortal() — cached "ip:port", so we can register pre-query
#include "../webconfig.h"    // registrationJson() — the identity payload (reused, not duplicated)
#include "ota.h"             // otaHostname() — the stable id (matches the register payload)
#include "updater.h"         // updaterAvailable* — OTA pull status carried in the heartbeat
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_arduino_version.h>   // ESP_ARDUINO_VERSION_MAJOR — see mdnsResultIp() below
#include "logmirror.h"   // LOG — tees to the TCP mirror where enabled, plain Serial otherwise

// Arduino-ESP32 3.x renamed MDNSResponder::IP(idx) to address(idx). This file has to compile on
// both: the ESP32-S3 units (nest, sleep-machine) are pinned to Arduino 2.0.17, while sonos-jukebox
// is ESP32-P4 and can only build on 3.x. Keep this shim rather than bumping the S3 units — see
// plans/07-sonos-jukebox.md for why those pins are load-bearing.
//
// The fallback is NOT belt-and-braces. Both accessors just walk the addresses attached to the
// service result and return an empty IPAddress if none is IPv4 — so a responder that answers the
// PTR/SRV query without also inlining an A record yields 0.0.0.0, and the device then cheerfully
// "registers" with the portal at 0.0.0.0. That is what this board did on its first real boot.
// Resolving the advertised hostname separately is the reliable path.
static inline IPAddress mdnsResultIp(int idx) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  IPAddress ip = MDNS.address(idx);
#else
  IPAddress ip = MDNS.IP(idx);
#endif
  if (ip == IPAddress((uint32_t)0)) {
    String host = MDNS.hostname(idx);
    if (host.length()) {
      ip = MDNS.queryHost(host);
      LOG.printf("[registrar] service result had no A record; resolved %s -> %s\n",
                    host.c_str(), ip.toString().c_str());
    }
  }
  return ip;
}

// Firmware version string — injected per build by tools/git_version.py (git describe). Default
// keeps a plain `pio run` from any checkout compiling; the real value comes from the build flag.
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// The portal's mDNS service (advertised by the server as `_sonosportal._tcp`). ESP32's
// MDNS.queryService() wants the bare label with no leading underscore, so serviceName() strips
// one if a build passed -DPORTAL_SERVICE='"_sonosportal"' (the form the plan/manifest use).
#ifndef PORTAL_SERVICE
#define PORTAL_SERVICE "sonosportal"
#endif

static const uint32_t kHeartbeatMs = 45000;   // ~45 s: 2-3 misses (~2 min) flips the tile offline

static String   s_host;      // resolved portal IP ("" until we've found one)
static uint16_t s_port = 0;

// Backoff for portal resolution. resolvePortal() runs a blocking mDNS service query and, when the
// responder answers without an A record, a second blocking queryHost() on top (see mdnsResultIp).
// Both run on netTask, which is also draining g_pending and polling Sonos — so on a LAN with no
// portal at all, retrying every heartbeat stalls playback control for seconds, every 45 s, forever.
// The portal is optional; failing to find one must stay cheap. Doubles 45 s -> ~12 min and holds.
static uint8_t  s_resolveFails = 0;
static uint32_t s_nextResolveMs = 0;
static const uint32_t kResolveBackoffMaxMs = 720000;

static const char *serviceName() {
  const char *s = PORTAL_SERVICE;
  return (s[0] == '_') ? s + 1 : s;   // queryService() adds the underscore itself
}

// Find the portal: mDNS first (authoritative, picks up an IP change), else the last-known ip:port
// from NVS (lets us register on a boot where the query hasn't resolved yet). Caches an mDNS hit.
static bool resolvePortal() {
  int n = MDNS.queryService(serviceName(), "tcp");
  if (n > 0) {
    IPAddress ip = mdnsResultIp(0);
    if (ip == IPAddress((uint32_t)0)) {
      // Never cache 0.0.0.0: settingsSetPortal() would persist it to NVS and every later boot
      // would "resolve" the portal to a dead address without ever retrying mDNS.
      LOG.println("[registrar] mDNS hit but no usable address — falling back to cache");
    } else {
      s_host = ip.toString();
      s_port = MDNS.port(0);
      settingsSetPortal(s_host + ":" + String(s_port));
      LOG.printf("[registrar] portal @ %s:%u (mDNS)\n", s_host.c_str(), s_port);
      return true;
    }
  }
  String cached = settingsPortal();
  int c = cached.indexOf(':');
  if (c > 0) {
    s_host = cached.substring(0, c);
    s_port = (uint16_t)cached.substring(c + 1).toInt();
    if (s_host.length() && s_port) {
      LOG.printf("[registrar] portal @ %s:%u (cached)\n", s_host.c_str(), s_port);
      return true;
    }
  }
  s_host = ""; s_port = 0;
  return false;
}

// POST a JSON body to the resolved portal. Short timeouts: this runs on netTask between Sonos
// polls, so a slow/absent portal must not stall input. Returns true on a 2xx/3xx; when respOut is
// given, fills it with the response body (used to read the heartbeat's "recheck" nudge).
static bool httpPostJson(const char *path, const String &body, String *respOut = nullptr) {
  if (s_host.length() == 0 || s_port == 0) return false;
  WiFiClient client;
  HTTPClient http;
  String url = "http://" + s_host + ":" + String(s_port) + path;
  if (!http.begin(client, url)) return false;
  http.setConnectTimeout(2000);
  http.setTimeout(3000);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  if (respOut && code > 0) *respOut = http.getString();
  http.end();
  return code >= 200 && code < 400;
}

static void postRegister() {
  if (httpPostJson("/api/register", registrationJson()))
    LOG.println("[registrar] registered with portal");
  else
    LOG.println("[registrar] register POST failed");
}

// Minimal liveness payload. The stable id (mdnsName) matches the register payload so the portal
// upserts the same row rather than creating a duplicate.
static String heartbeatJson() {
  JsonDocument doc;
  doc["mdnsName"]  = String(otaHostname()) + ".local";
  doc["ip"]        = WiFi.localIP().toString();
  doc["uptimeSec"] = (uint32_t)(millis() / 1000);
  doc["fwVersion"] = FW_VERSION;
  // OTA pull status, so the dashboard reflects policy + a waiting update between registrations.
  doc["otaAuto"]   = settingsOtaAuto();
  if (updaterAvailable()) doc["updateAvailable"] = updaterAvailableVersion();
  else                    doc["updateAvailable"] = (const char *)nullptr;
  String out;
  serializeJson(doc, out);
  return out;
}

void registrarBegin() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!resolvePortal()) {
    LOG.println("[registrar] portal not found yet — will retry on heartbeat");
    return;
  }
  postRegister();
}

void registrarTick() {
  static uint32_t last = 0;
  if (millis() - last < kHeartbeatMs) return;
  last = millis();
  if (WiFi.status() != WL_CONNECTED) return;

  // Never resolved (portal started after us): resolve + full register, not a heartbeat.
  if (s_host.length() == 0 || s_port == 0) {
    if (s_nextResolveMs && (int32_t)(millis() - s_nextResolveMs) < 0) return;   // backing off
    if (resolvePortal()) {
      s_resolveFails = 0;
      s_nextResolveMs = 0;
      postRegister();
    } else {
      if (s_resolveFails < 8) s_resolveFails++;
      uint32_t wait = kHeartbeatMs << s_resolveFails;
      if (wait > kResolveBackoffMaxMs) wait = kResolveBackoffMaxMs;
      s_nextResolveMs = millis() + wait;
      LOG.printf("[registrar] portal not found (%u) — next mDNS attempt in %lus\n",
                    (unsigned)s_resolveFails, (unsigned long)(wait / 1000));
    }
    return;
  }
  // A failed heartbeat means the portal restarted, moved, or forgot us — drop the cached host so
  // the next tick re-resolves via mDNS and re-registers from scratch.
  String resp;
  if (!httpPostJson("/api/heartbeat", heartbeatJson(), &resp)) {
    LOG.println("[registrar] heartbeat failed — will re-resolve portal");
    s_host = ""; s_port = 0;
    // We were talking to a portal a moment ago, so it is worth one prompt re-resolve; don't
    // inherit a long backoff from whenever this device last booted with no portal on the LAN.
    s_resolveFails = 0; s_nextResolveMs = 0;
    return;
  }
  // The portal sets "recheck" when a firmware update has been approved for us from the dashboard.
  // The updater otherwise polls the manifest only every ~6 h; this makes an approval land within a
  // heartbeat (~45 s). Cheap to parse — the body is a few bytes.
  JsonDocument rd;
  if (!deserializeJson(rd, resp) && rd["recheck"].as<bool>()) {
    LOG.println("[registrar] portal requests firmware re-check");
    updaterForceCheck();
  }
}
