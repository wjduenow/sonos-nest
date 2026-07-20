// See registrar.h. Outbound self-registration with the sonos-portal dashboard.
#include "registrar.h"
#include "../settings.h"     // settingsPortal() — cached "ip:port", so we can register pre-query
#include "../webconfig.h"    // registrationJson() — the identity payload (reused, not duplicated)
#include "ota.h"             // otaHostname() — the stable id (matches the register payload)
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>

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

static const char *serviceName() {
  const char *s = PORTAL_SERVICE;
  return (s[0] == '_') ? s + 1 : s;   // queryService() adds the underscore itself
}

// Find the portal: mDNS first (authoritative, picks up an IP change), else the last-known ip:port
// from NVS (lets us register on a boot where the query hasn't resolved yet). Caches an mDNS hit.
static bool resolvePortal() {
  int n = MDNS.queryService(serviceName(), "tcp");
  if (n > 0) {
    s_host = MDNS.IP(0).toString();
    s_port = MDNS.port(0);
    settingsSetPortal(s_host + ":" + String(s_port));
    Serial.printf("[registrar] portal @ %s:%u (mDNS)\n", s_host.c_str(), s_port);
    return true;
  }
  String cached = settingsPortal();
  int c = cached.indexOf(':');
  if (c > 0) {
    s_host = cached.substring(0, c);
    s_port = (uint16_t)cached.substring(c + 1).toInt();
    if (s_host.length() && s_port) {
      Serial.printf("[registrar] portal @ %s:%u (cached)\n", s_host.c_str(), s_port);
      return true;
    }
  }
  s_host = ""; s_port = 0;
  return false;
}

// POST a JSON body to the resolved portal. Short timeouts: this runs on netTask between Sonos
// polls, so a slow/absent portal must not stall input. Returns true on a 2xx/3xx.
static bool httpPostJson(const char *path, const String &body) {
  if (s_host.length() == 0 || s_port == 0) return false;
  WiFiClient client;
  HTTPClient http;
  String url = "http://" + s_host + ":" + String(s_port) + path;
  if (!http.begin(client, url)) return false;
  http.setConnectTimeout(2000);
  http.setTimeout(3000);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  http.end();
  return code >= 200 && code < 400;
}

static void postRegister() {
  if (httpPostJson("/api/register", registrationJson()))
    Serial.println("[registrar] registered with portal");
  else
    Serial.println("[registrar] register POST failed");
}

// Minimal liveness payload. The stable id (mdnsName) matches the register payload so the portal
// upserts the same row rather than creating a duplicate.
static String heartbeatJson() {
  JsonDocument doc;
  doc["mdnsName"]  = String(otaHostname()) + ".local";
  doc["ip"]        = WiFi.localIP().toString();
  doc["uptimeSec"] = (uint32_t)(millis() / 1000);
  doc["fwVersion"] = FW_VERSION;
  String out;
  serializeJson(doc, out);
  return out;
}

void registrarBegin() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!resolvePortal()) {
    Serial.println("[registrar] portal not found yet — will retry on heartbeat");
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
    if (resolvePortal()) postRegister();
    return;
  }
  // A failed heartbeat means the portal restarted, moved, or forgot us — drop the cached host so
  // the next tick re-resolves via mDNS and re-registers from scratch.
  if (!httpPostJson("/api/heartbeat", heartbeatJson())) {
    Serial.println("[registrar] heartbeat failed — will re-resolve portal");
    s_host = ""; s_port = 0;
  }
}
