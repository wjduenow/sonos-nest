#include "ssdp.h"
#include <Arduino.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

// SSDP discovery of Sonos ZonePlayers. See plan §3.
// TODO Phase 4: persist discovered IPs/UUIDs in NVS and use cache-first on boot; refresh
//               group topology (coordinators) via ZoneGroupTopology.

namespace sonos {

static std::vector<Zone> s_zones;

// Value of an HTTP header line ("Name:") up to end-of-line, case-insensitive on the name.
static String headerValue(const String &resp, const char *name) {
  String lower = resp;  lower.toLowerCase();
  String key = name;    key.toLowerCase();
  int i = lower.indexOf(key);
  if (i < 0) return "";
  i += key.length();
  int eol = resp.indexOf('\r', i);
  if (eol < 0) eol = resp.indexOf('\n', i);
  if (eol < 0) eol = resp.length();
  String v = resp.substring(i, eol);
  v.trim();
  return v;
}

// "http://192.168.1.50:1400/..." -> "192.168.1.50"
static String ipFromLocation(const String &loc) {
  int s = loc.indexOf("//");
  if (s < 0) return "";
  s += 2;
  int e = loc.indexOf(':', s);
  if (e < 0) e = loc.indexOf('/', s);
  if (e < 0) return "";
  return loc.substring(s, e);
}

// GET the device description to fill in room name + UUID (RINCON_...).
static void fetchDescription(Zone &z) {
  WiFiClient client;
  HTTPClient http;
  String url = "http://" + z.ip + ":1400/xml/device_description.xml";
  if (!http.begin(client, url)) return;
  if (http.GET() == 200) {
    String body = http.getString();
    int a = body.indexOf("<roomName>");
    if (a >= 0) { a += 10; int b = body.indexOf("</roomName>", a); if (b > a) z.name = body.substring(a, b); }
    int u = body.indexOf("<UDN>");
    if (u >= 0) { u += 5;  int b = body.indexOf("</UDN>", u); if (b > u) { z.uuid = body.substring(u, b); z.uuid.replace("uuid:", ""); } }
  }
  http.end();
}

bool ssdpDiscover() {
  s_zones.clear();

#ifdef SONOS_ZONE_IP
  // Dev override: skip multicast, talk straight to a known speaker (plan §7).
  {
    Zone z;
    z.ip = SONOS_ZONE_IP;
    z.name = "(configured)";
    z.isCoordinator = true;
    fetchDescription(z);
    s_zones.push_back(z);
    return true;
  }
#endif

  WiFiUDP udp;
  udp.begin(1900);
  IPAddress mcast(239, 255, 255, 250);
  static const char *msearch =
      "M-SEARCH * HTTP/1.1\r\n"
      "HOST: 239.255.255.250:1900\r\n"
      "MAN: \"ssdp:discover\"\r\n"
      "MX: 1\r\n"
      "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
      "\r\n";

  for (int attempt = 0; attempt < 2 && s_zones.empty(); ++attempt) {
    udp.beginPacket(mcast, 1900);
    udp.write((const uint8_t *)msearch, strlen(msearch));
    udp.endPacket();

    uint32_t deadline = millis() + 1500;
    while ((int32_t)(deadline - millis()) > 0) {
      int sz = udp.parsePacket();
      if (sz > 0) {
        char buf[1024];
        int n = udp.read(buf, sizeof(buf) - 1);
        if (n > 0) {
          buf[n] = 0;
          String resp(buf);
          String loc = headerValue(resp, "LOCATION:");
          String ip = loc.length() ? ipFromLocation(loc) : String();
          if (ip.length()) {
            bool dup = false;
            for (auto &z : s_zones) if (z.ip == ip) { dup = true; break; }
            if (!dup) { Zone z; z.ip = ip; s_zones.push_back(z); }
          }
        }
      }
      delay(5);
    }
  }
  udp.stop();

  for (auto &z : s_zones) fetchDescription(z);
  return !s_zones.empty();
}

const std::vector<Zone> &zones() { return s_zones; }

String coordinatorIpFor(const String &zoneName) {
  // TODO Phase 4: resolve the real group coordinator via ZoneGroupTopology. For now a zone
  // maps to its own IP (correct for ungrouped speakers).
  for (auto &z : s_zones) if (z.name == zoneName) return z.ip;
  return s_zones.empty() ? String() : s_zones[0].ip;
}

}  // namespace sonos
