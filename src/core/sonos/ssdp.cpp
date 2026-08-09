#include "ssdp.h"
#include "soap_client.h"
#include "../settings.h"
#include <Arduino.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <algorithm>
#include <strings.h>

// SSDP discovery of Sonos ZonePlayers. See plan §3.
// TODO Phase 4: persist discovered IPs/UUIDs in NVS and use cache-first on boot; refresh
//               group topology (coordinators) via ZoneGroupTopology.

namespace sonos {

// s_zones is written by netTask (discovery) and read by other tasks: every unit's UI task paints
// a room list from it, and on boards with a web UI the HTTP server task reads it too. Discovery
// does clear() + push_back(), so an unguarded reader can walk destroyed Strings or a reallocated
// buffer mid-rebuild — garbage room names at best, LoadProhibited at worst.
//
// The rule here: BUILD INTO A LOCAL, SWAP UNDER THE LOCK. The lock is never held across network
// I/O — a topology fetch takes hundreds of ms and blocking the UI for that would trade a rare
// crash for a guaranteed stutter. Readers on another task take a copy (zonesSnapshot); the list is
// a handful of small structs, so the copy is far cheaper than the alternatives.
static std::vector<Zone> s_zones;

// Function-local static: C++11 guarantees thread-safe initialisation, and the first call happens
// during appBoot() before any other task exists anyway.
static SemaphoreHandle_t zonesLock() {
  static SemaphoreHandle_t m = xSemaphoreCreateMutex();
  return m;
}
struct ZonesGuard {
  ZonesGuard()  { xSemaphoreTake(zonesLock(), portMAX_DELAY); }
  ~ZonesGuard() { xSemaphoreGive(zonesLock()); }
};

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

static String unescapeXml(String s) {
  s.replace("&lt;", "<");
  s.replace("&gt;", ">");
  s.replace("&quot;", "\"");
  s.replace("&apos;", "'");
  s.replace("&amp;", "&");  // last, so it doesn't double-decode the others
  return s;
}

// Value of an element's   name="value"   attribute.
static String attrOf(const String &s, const char *name) {
  String key = String(name) + "=\"";
  int i = s.indexOf(key);
  if (i < 0) return "";
  i += key.length();
  int e = s.indexOf('"', i);
  return e < 0 ? "" : s.substring(i, e);
}

// Build the canonical room list from ZoneGroupTopology: one entry per VISIBLE room. Bonded
// stereo-pair satellites (<Satellite> children) and invisible devices (bridges, hidden
// subs) are excluded, so a paired room appears once — matching the Sonos app.
static bool buildZonesFromTopology(const String &seedIp) {
  std::vector<Zone> next;   // built here, swapped into s_zones at the end — see the note above
  String r;
  if (!soapAction(seedIp, "/ZoneGroupTopology/Control",
                  "urn:schemas-upnp-org:service:ZoneGroupTopology:1",
                  "GetZoneGroupState", "", r))
    return false;
  int gs = r.indexOf("<ZoneGroupState>");
  int ge = r.indexOf("</ZoneGroupState>");
  if (gs < 0 || ge < 0) return false;
  String state = unescapeXml(r.substring(gs + 16, ge));

  int gpos = 0;
  while ((gpos = state.indexOf("<ZoneGroup ", gpos)) >= 0) {
    int gend = state.indexOf("</ZoneGroup>", gpos);
    if (gend < 0) gend = state.length();
    String group = state.substring(gpos, gend);
    gpos = gend + 1;

    String coordUuid = attrOf(group, "Coordinator");
    size_t firstIdx = next.size();

    // Top-level <ZoneGroupMember ...> = a visible room; <Satellite> children aren't matched.
    int mpos = 0;
    while ((mpos = group.indexOf("<ZoneGroupMember ", mpos)) >= 0) {
      int mend = group.indexOf('>', mpos);
      if (mend < 0) break;
      String m = group.substring(mpos, mend);
      mpos = mend + 1;
      if (attrOf(m, "Invisible") == "1") continue;
      String name = attrOf(m, "ZoneName");
      String loc  = attrOf(m, "Location");
      if (!name.length() || !loc.length()) continue;
      Zone z;
      z.name = name;
      z.ip = ipFromLocation(loc);
      z.uuid = attrOf(m, "UUID");
      z.coordinatorUuid = coordUuid;
      z.isCoordinator = (z.uuid == coordUuid);
      next.push_back(z);
    }
    // Stamp this group's coordinator IP onto its members.
    String coordIp;
    for (size_t i = firstIdx; i < next.size(); ++i)
      if (next[i].uuid == coordUuid) coordIp = next[i].ip;
    for (size_t i = firstIdx; i < next.size(); ++i) next[i].coordIp = coordIp;
  }
  std::sort(next.begin(), next.end(), [](const Zone &a, const Zone &b) {
    return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
  });
  if (next.empty()) return false;
  { ZonesGuard g; s_zones.swap(next); }   // the only mutation readers can observe
  return true;
}

// Find one reachable Sonos via SSDP — just a seed to query topology from.
static String ssdpSeed() {
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
  String seed;
  for (int attempt = 0; attempt < 3 && seed.length() == 0; ++attempt) {
    udp.beginPacket(mcast, 1900);
    udp.write((const uint8_t *)msearch, strlen(msearch));
    udp.endPacket();
    uint32_t deadline = millis() + 1200;
    while ((int32_t)(deadline - millis()) > 0 && seed.length() == 0) {
      if (udp.parsePacket() > 0) {
        char buf[1024];
        int n = udp.read(buf, sizeof(buf) - 1);
        if (n > 0) {
          buf[n] = 0;
          String resp(buf);
          String loc = headerValue(resp, "LOCATION:");
          if (loc.length()) seed = ipFromLocation(loc);
        }
      }
      delay(5);
    }
  }
  udp.stop();
  return seed;
}

// Serialize/deserialize the zone list for the NVS cache (fast boot). One zone per line,
// fields separated by US (0x1f): name | ip | uuid | coordinatorUuid | coordIp.
static String serializeZones() {
  std::vector<Zone> zs;
  zonesSnapshot(zs);
  String s;
  for (auto &z : zs) {
    s += z.name; s += '\x1f'; s += z.ip; s += '\x1f'; s += z.uuid; s += '\x1f';
    s += z.coordinatorUuid; s += '\x1f'; s += z.coordIp; s += '\n';
  }
  return s;
}
static bool deserializeZones(const String &blob) {
  std::vector<Zone> next;
  int pos = 0;
  while (pos < (int)blob.length()) {
    int eol = blob.indexOf('\n', pos);
    if (eol < 0) break;
    String line = blob.substring(pos, eol);
    pos = eol + 1;
    Zone z;
    int field = 0, start = 0;
    for (int i = 0; i <= (int)line.length(); ++i) {
      if (i == (int)line.length() || line[i] == '\x1f') {
        String v = line.substring(start, i);
        start = i + 1;
        switch (field++) {
          case 0: z.name = v; break;
          case 1: z.ip = v; break;
          case 2: z.uuid = v; break;
          case 3: z.coordinatorUuid = v; break;
          case 4: z.coordIp = v; break;
        }
      }
    }
    if (z.name.length() && z.ip.length()) next.push_back(z);
  }
  if (next.empty()) return false;
  { ZonesGuard g; s_zones.swap(next); }
  return true;
}

// Discover the canonical rooms. Fast path: refresh topology from a CACHED speaker IP (no
// SSDP multicast wait). Fallback: SSDP to find any speaker, then enumerate from topology.
// Either way, the fresh list is cached to NVS for next boot.
bool ssdpDiscover() {
#ifdef SONOS_ZONE_IP
  if (buildZonesFromTopology(SONOS_ZONE_IP)) { settingsSetZones(serializeZones()); return true; }
#endif
  // Fast path: a cached IP is usually still valid — one topology fetch, no 3.6s SSDP wait.
  if (deserializeZones(settingsZones()) && zoneCount()) {
    String seed;
    { ZonesGuard g; seed = s_zones[0].ip; }
    if (buildZonesFromTopology(seed)) { settingsSetZones(serializeZones()); return true; }
  }
  // Slow path: full SSDP discovery (network changed, or first boot).
  String seed = ssdpSeed();
  if (seed.length() == 0) { ZonesGuard g; s_zones.clear(); return false; }
  if (!buildZonesFromTopology(seed)) return false;
  settingsSetZones(serializeZones());
  return true;
}

const std::vector<Zone> &zones() { return s_zones; }

void zonesSnapshot(std::vector<Zone> &out) {
  ZonesGuard g;
  out = s_zones;
}

size_t zoneCount() {
  ZonesGuard g;
  return s_zones.size();
}

// The IP of the speaker that returned a zone's own IP (volume target fallback).
static String zoneOwnIp(const String &zoneName) {
  for (auto &z : s_zones) if (z.name == zoneName) return z.ip;
  return s_zones.empty() ? String() : s_zones[0].ip;
}

String coordinatorIpFor(const String &zoneName) {
  // Transport/queue commands must target the group coordinator (plan §3). Query
  // ZoneGroupTopology, find the group containing this zone, and resolve its coordinator IP.
  if (s_zones.empty()) return "";
  String r;
  if (!soapAction(s_zones[0].ip, "/ZoneGroupTopology/Control",
                  "urn:schemas-upnp-org:service:ZoneGroupTopology:1",
                  "GetZoneGroupState", "", r)) {
    return zoneOwnIp(zoneName);  // fallback: assume ungrouped
  }

  // <ZoneGroupState> wraps an escaped XML doc of <ZoneGroup Coordinator="RINCON_x" ...>
  //   <ZoneGroupMember UUID="RINCON_y" Location="http://ip:1400/..." ZoneName="..."/> ... </ZoneGroup>
  int gs = r.indexOf("<ZoneGroupState>");
  int ge = r.indexOf("</ZoneGroupState>");
  if (gs < 0 || ge < 0) return zoneOwnIp(zoneName);
  String state = unescapeXml(r.substring(gs + 16, ge));

  // Find this zone's member, then the nearest preceding group Coordinator UUID.
  int pn = state.indexOf("ZoneName=\"" + zoneName + "\"");
  if (pn < 0) return zoneOwnIp(zoneName);
  int cg = state.lastIndexOf("Coordinator=\"", pn);
  if (cg < 0) return zoneOwnIp(zoneName);
  cg += 13;  // strlen("Coordinator=\"")
  int ce = state.indexOf('"', cg);
  if (ce < 0) return zoneOwnIp(zoneName);
  String coordUuid = state.substring(cg, ce);

  // Resolve the coordinator member's Location -> IP.
  int pu = state.indexOf("UUID=\"" + coordUuid + "\"");
  if (pu < 0) return zoneOwnIp(zoneName);
  int pl = state.indexOf("Location=\"", pu);
  if (pl < 0) return zoneOwnIp(zoneName);
  pl += 10;  // strlen("Location=\"")
  String loc = state.substring(pl, state.indexOf('"', pl));
  String ip = ipFromLocation(loc);
  return ip.length() ? ip : zoneOwnIp(zoneName);
}

}  // namespace sonos
