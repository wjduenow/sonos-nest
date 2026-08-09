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
  String ip;            // this room's primary speaker (volume target)
  String uuid;          // RINCON_...
  String coordinatorUuid;
  String coordIp;       // group coordinator's IP (transport target)
  bool   isCoordinator = false;
};

// Discover players (UDP M-SEARCH to 239.255.255.250:1900,
// ST urn:schemas-upnp-org:device:ZonePlayer:1). Falls back to NVS cache.
bool ssdpDiscover();

// *** netTask ONLY. *** Returns a reference to the live list, which discovery rewrites. Any other
// task must use zonesSnapshot() instead — reading this reference while netTask is rediscovering
// walks destroyed Strings or a reallocated buffer (garbage names, or LoadProhibited).
const std::vector<Zone>& zones();

// Thread-safe copy of the current room list. This is what UI tasks and board web servers want.
// Costs one vector copy of a handful of small structs; call it when rebuilding, not per frame.
void zonesSnapshot(std::vector<Zone>& out);

// Thread-safe count, for the common case of only needing the size (health logs, empty checks).
size_t zoneCount();

// Resolve a zone name to the IP of its group coordinator (for transport calls).
String coordinatorIpFor(const String& zoneName);

// TODO Phase 4: refresh topology via ZoneGroupTopology service for grouping changes.

}  // namespace sonos
