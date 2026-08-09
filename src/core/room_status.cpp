// See room_status.h for why this is throttled the way it is.
#include "room_status.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "sonos/soap_client.h"
#include "sonos/ssdp.h"

namespace roomstatus {

// Two cadences, because the FIRST round and the refresh rounds want opposite things.
//
// The first round is the one the user is watching: every row reads "--" until its room is polled,
// so any gate here is dead time on screen. A step costs ~150 ms of SOAP already (getVolume plus,
// on a coordinator, getTransportInfo — the second reuses the socket because it is the same host),
// and netTask drains processPending() once per loop regardless, so commands are still serviced
// between rooms. A near-zero gate just stops it being a hard spin.
//
// Once every room has been read once, nothing on screen is blank and the poll exists only to
// catch changes made elsewhere (a phone, the Sonos app) — so it backs right off. This is what
// keeps the steady-state cost near netTask's own 1 Hz poll instead of tripling it.
//
// NB the switch is driven by "the cursor has wrapped once", NOT "every room has a reading": a room
// that is off or unreachable never gets one, and gating on that would leave the fast cadence
// running forever against a speaker that is never going to answer.
static const uint32_t kFillStepMs = 30;
static const uint32_t kIdleStepMs = 400;

// How long a locally-set volume wins over an incoming poll result. Must comfortably exceed one
// SOAP round-trip (typical 26-129 ms, seen at 5926 ms) or a slow in-flight read still clobbers it;
// must stay short enough that a change made on a phone shows up promptly. See Room::volHoldUntil.
static const uint32_t kVolHoldMs = 3000;

static std::vector<Room> s_rooms;
static SemaphoreHandle_t s_lock = nullptr;
static volatile uint32_t s_gen = 0;
static uint32_t s_wantUntil = 0;      // millis() past which nobody is asking; see keepAlive()
static uint32_t s_lastStepMs = 0;
static uint32_t s_zonesGenSeen = UINT32_MAX;
static size_t   s_cursor = 0;
static bool     s_roundDone = false;   // a full round has completed since the last invalidate
static size_t   s_stepsThisRound = 0;  // counted, not compared against 0: prioritise() can start
                                       // the round anywhere, so "cursor wrapped to 0" is not it

static bool lock() {
  if (!s_lock) s_lock = xSemaphoreCreateMutex();
  return s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}
static void unlock() { if (s_lock) xSemaphoreGive(s_lock); }

void keepAlive() { s_wantUntil = millis() + 2000; }

uint32_t gen() { return s_gen; }

// NOTE: there is deliberately no invalidate()/"drop everything after a grouping change" call.
// There was one, and it was the single biggest source of perceived lag on the Rooms page: every
// checkbox tap blanked all nine rooms back to "--" and made the whole page refill over ~1.4 s.
// It is also unnecessary. Volume is a per-speaker property that grouping does not touch, and
// transport is re-derived by snapshot() from whichever coordinator each room now follows — see
// syncTopology(), which carries both across a topology change on purpose.

// Deferred on purpose. The Rooms page calls prioritise() the moment it opens, which is BEFORE
// netTask has run a single tick() — so the table is still empty and there is nothing to point the
// cursor at. Remembering the request and applying it when the table first appears is what makes
// the hint work on the first open, which is the only open where it matters.
static String s_priorityIp;

static void applyPriorityLocked() {
  if (!s_priorityIp.length()) return;
  for (size_t i = 0; i < s_rooms.size(); ++i) {
    if (s_rooms[i].ip != s_priorityIp) continue;
    s_cursor = i;
    s_priorityIp = "";
    return;
  }
}

void prioritise(const String &ip) {
  if (!lock()) return;
  s_priorityIp = ip;
  applyPriorityLocked();
  unlock();
}

void noteVolume(const String &ip, uint8_t vol) {
  if (!lock()) return;
  for (auto &r : s_rooms) {
    if (r.ip == ip) { r.vol = vol; r.volOk = true; r.volHoldUntil = millis() + kVolHoldMs; break; }
  }
  unlock();
  s_gen++;
}

// Rebuild the table from discovery, carrying forward readings for rooms that are still present.
// A grouping change bumps g_zonesGen and rewrites every coordinator field, but the VOLUMES are
// still valid — dropping them would blank every bar on the page each time a checkbox is tapped.
static void syncTopology() {
  std::vector<sonos::Zone> zs;
  sonos::zonesSnapshot(zs);

  std::vector<Room> next;
  next.reserve(zs.size());
  for (const auto &z : zs) {
    Room r;
    r.name = z.name;
    r.ip = z.ip;
    r.uuid = z.uuid;
    r.coordinatorUuid = z.coordinatorUuid;
    r.coordIp = z.coordIp;
    r.isCoordinator = z.isCoordinator;
    for (const auto &old : s_rooms) {
      if (old.ip != r.ip) continue;
      r.vol = old.vol; r.volOk = old.volOk; r.volHoldUntil = old.volHoldUntil;
      // Transport carries over ONLY for a room that was a coordinator and still is: its group's
      // play state is unchanged by whatever moved around it. A room that just joined a group gets
      // the right answer from snapshot(), which overwrites members from their coordinator; a room
      // that just BECAME a coordinator (split off) genuinely has an unknown state, so it is
      // dropped and shows "--" for one poll rather than asserting the old group's state.
      if (old.isCoordinator && r.isCoordinator) {
        r.transport = old.transport; r.transportOk = old.transportOk;
      }
      break;
    }
    next.push_back(r);
  }
  s_rooms.swap(next);
  if (s_cursor >= s_rooms.size()) s_cursor = 0;
  // A new room set is a new round — and it may contain rooms that have never been read.
  s_stepsThisRound = 0;
  s_roundDone = false;
  applyPriorityLocked();   // the page's hint may have arrived before this table existed
}

void tick() {
  if ((int32_t)(millis() - s_wantUntil) > 0) return;   // nobody is on the Rooms page

  if (!lock()) return;
  if (s_zonesGenSeen != g_zonesGen) {
    s_zonesGenSeen = g_zonesGen;
    syncTopology();
    unlock();
    s_gen++;
    return;    // give the UI a frame to show the new room set before spending SOAP on it
  }

  const uint32_t step = s_roundDone ? kIdleStepMs : kFillStepMs;
  if (s_rooms.empty() || millis() - s_lastStepMs < step) { unlock(); return; }
  s_lastStepMs = millis();

  // Copy the targets out and release the lock: the SOAP calls below block for tens to thousands
  // of ms, and holding this across them would stall every snapshot() the UI task makes.
  const size_t idx = s_cursor;
  s_cursor = (s_cursor + 1) % s_rooms.size();
  if (++s_stepsThisRound >= s_rooms.size()) s_roundDone = true;   // fully populated: back off
  const String ip      = s_rooms[idx].ip;
  const String coordIp = s_rooms[idx].coordIp;
  const bool   isCoord = s_rooms[idx].isCoordinator;
  unlock();

  uint8_t vol = 0;
  const bool volOk = sonos::getVolume(ip, vol);

  // Only coordinators are asked about transport — members share it (see room_status.h).
  TransportState st = TransportState::Unknown;
  bool stOk = false;
  if (isCoord && coordIp.length()) stOk = sonos::getTransportInfo(coordIp, st);

  if (!lock()) return;
  // The table may have been rebuilt while we were on the wire — match by IP, not by index.
  for (auto &r : s_rooms) {
    if (r.ip != ip) continue;
    // Don't let a read that was already on the wire undo a volume the user just set.
    const bool held = (int32_t)(millis() - r.volHoldUntil) < 0;
    if (volOk && !held) { r.vol = vol; r.volOk = true; }
    if (stOk) { r.transport = st; r.transportOk = true; }
    break;
  }
  unlock();
  s_gen++;
}

void snapshot(std::vector<Room> &out) {
  out.clear();
  if (!lock()) return;
  out = s_rooms;
  unlock();

  // Propagate each coordinator's transport to its members, and count group sizes. Done here rather
  // than in tick() so it is always consistent with the table as handed to the UI, even mid-round.
  for (auto &r : out) {
    uint8_t n = 0;
    for (const auto &o : out) if (o.coordinatorUuid == r.coordinatorUuid) n++;
    r.groupSize = n ? n : 1;

    if (r.isCoordinator) continue;
    for (const auto &c : out) {
      if (!c.isCoordinator || c.uuid != r.coordinatorUuid) continue;
      r.transport = c.transport;
      r.transportOk = c.transportOk;
      break;
    }
  }
}

}  // namespace roomstatus
