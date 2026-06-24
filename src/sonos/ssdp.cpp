#include "ssdp.h"
#include <WiFiUdp.h>

// TODO Phase 1: send M-SEARCH, collect 200 OK responses, fetch each player's device
//               description for name/UUID. Persist to NVS (Preferences).
// TODO Phase 1: load cache on boot; only M-SEARCH if cache empty or stale/unreachable.
// TODO Phase 4: parse ZoneGroupTopology to track coordinators across grouping changes.

namespace sonos {

static std::vector<Zone> s_zones;

bool ssdpDiscover() {
  // TODO: cache-first discovery.
  return false;
}

const std::vector<Zone>& zones() { return s_zones; }

String coordinatorIpFor(const String&) {
  return String();  // TODO
}

}  // namespace sonos
