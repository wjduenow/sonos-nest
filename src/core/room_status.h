// Per-room live status for the Rooms screen — volume and playing state for EVERY room, not just
// the controlled one.
//
// WHY THIS EXISTS AS ITS OWN MODULE. netTask's 1 Hz poll (core/app.cpp) only ever talks to the
// active zone: getTransportInfo/getPositionInfo on the coordinator and getVolume on the speaker.
// Every other room has no live state at all — the Rooms page renders purely from the cached
// topology in sonos::zonesSnapshot(), with no SOAP behind it. Showing per-room volume and
// play state means new calls, and they are NOT free: a nine-room house needs ~9 getVolume plus
// one getTransportInfo per distinct group, and measured SOAP latency on this unit runs 26-129 ms
// typical but has been seen at 5926 ms. Issued in a burst on page entry that would stall netTask
// for seconds — blocking the transport commands the user is pressing RIGHT THEN.
//
// So: one room per tick, gated to kStepMs, and only while the Rooms page is actually asking. The
// page fills in over a few seconds and then stays fresh, and the cost when nobody is looking at
// Rooms is exactly zero.
//
// THREADING. tick() is netTask ONLY (it does blocking SOAP). Everything else is safe from any
// task; the table is guarded by its own mutex, deliberately not g_stateMutex — this poller holds
// its lock across a vector copy, and sharing the player-state mutex would put that behind the same
// lock the UI takes every frame.
#pragma once

#include <Arduino.h>
#include <vector>

#include "player_state.h"   // TransportState

namespace roomstatus {

struct Room {
  String   name;
  String   ip;                // this room's own speaker — the VOLUME target
  String   uuid;
  String   coordinatorUuid;
  String   coordIp;           // group coordinator — the TRANSPORT target
  bool     isCoordinator = false;

  uint8_t  vol   = 0;
  bool     volOk = false;     // false until this room has been polled at least once

  // Transport is a property of the GROUP, not the room: Sonos has no way to pause one member of a
  // group. Only coordinators are polled; snapshot() copies the result down to their members, so
  // every Room carries the right value while costing one call per group rather than one per room.
  TransportState transport   = TransportState::Unknown;
  bool           transportOk = false;

  // How many rooms share this room's coordinator, itself included. 1 = ungrouped. Filled by
  // snapshot() so the UI doesn't have to count, and so "Playing · grouped" can be decided without
  // a second pass over the list.
  uint8_t groupSize = 1;

  // Internal bookkeeping, not for the UI to read. A getVolume() issued BEFORE the user's setVolume
  // landed returns the old level and would overwrite the optimistic value from noteVolume(),
  // snapping the bar backwards for a moment before the next round corrects it. Poll results for
  // this room are ignored until this expires. See noteVolume().
  uint32_t volHoldUntil = 0;
};

// The Rooms page calls this every uiTick while it is visible. Polling stops on its own ~2 s after
// the calls stop, so there is no "disable" to forget on every exit path (page switch, screensaver,
// OTA overlay) — and no way to leave the poller running forever against a page nobody is on.
void keepAlive();

// netTask ONLY. Polls at most one room per call and self-throttles; safe to call every loop.
void tick();

// Start the next round at this speaker rather than wherever the cursor happened to stop. The Rooms
// page calls it on entry with the ACTIVE room, so the group summary bar — which is the top of the
// screen and reads from that room — populates first instead of possibly last.
void prioritise(const String &ip);

// Thread-safe copy, with transport and groupSize propagated. What the UI renders from.
void snapshot(std::vector<Room> &out);

// Bumped whenever a poll changes anything. The UI re-renders on a change rather than every frame.
uint32_t gen();

// Optimistic local update after the UI posts a volume command, so the bar tracks the finger
// instead of waiting a poll round, and so repeated ± taps accumulate from the displayed value
// rather than all computing off the same stale reading.
void noteVolume(const String &ip, uint8_t vol);

// There is deliberately NO invalidate() — a grouping change must not blank the page. See the note
// where it used to live in room_status.cpp.

}  // namespace roomstatus
