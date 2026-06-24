// SSDP discovery of Sonos ZonePlayers + group topology. See plan §3.
//
// Strategy: SSDP M-SEARCH once -> cache zone IPs/UUIDs in NVS -> use cache on boot,
// re-run discovery only on failure. Multicast on ESP32 is flaky; cache-first avoids it.
#pragma once

#include <Arduino.h>
#include <vector>

namespace sonos {

struct Zone {
  String name;
  String ip;
  String uuid;          // RINCON_...
  String coordinatorUuid;
  bool   isCoordinator = false;
};

// Discover players (UDP M-SEARCH to 239.255.255.250:1900,
// ST urn:schemas-upnp-org:device:ZonePlayer:1). Falls back to NVS cache.
bool ssdpDiscover();

const std::vector<Zone>& zones();

// Resolve a zone name to the IP of its group coordinator (for transport calls).
String coordinatorIpFor(const String& zoneName);

// TODO Phase 4: refresh topology via ZoneGroupTopology service for grouping changes.

}  // namespace sonos
