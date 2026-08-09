// UPnP GENA eventing — Sonos pushes state changes to us instead of us polling for them.
// Full measurements and rationale: plans/09-gena-eventing.md. Issue #6.
//
// WHAT IT REPLACES. netTask polls the active zone ~1 Hz forever: GetTransportInfo +
// GetPositionInfo + GetVolume, about 10,800 SOAP round trips and ~13 MB an hour, whether or not
// anything changed. Measured against the real system, the same hour of eventing is ~17 events and
// ~85 KB — roughly 150x less. On the jukebox that is not tidiness: the ESP-Hosted link dies under
// sustained load (rssi=0 while wifi=3, unresolved upstream), and constant polling is exactly the
// implicated profile.
//
// OPT-IN PER ENV, behind -DGENA_EVENTS. Without it every entry point below is an inline no-op and
// gena.cpp is an empty translation unit. This is a direct lesson from issue #7: build_src_filter
// sweeps all of core/ into every env, so a core file that assumes resources the headless button
// does not have breaks only that env — the one nobody builds by habit. Guarded, the S3 units pay
// nothing and cannot break. Sizing per unit is in plans/09; the sleep-machine cannot afford this.
//
// !!! TWO THINGS THAT ARE NOT NEGOTIABLE, both measured !!!
//
//   1. POSITION IS NOT EVENTED. 83 seconds of playback produced no event until the track changed.
//      The progress bar must still be interpolated locally and reconciled when an event lands.
//      Eventing does not remove the timer, so do not delete it.
//
//   2. THE POLL IS A BACKSTOP, NOT DEAD CODE. Subscriptions lapse, links drop, speakers reboot.
//      Slow the poll down once events are proven; never remove it. A missed event with no backstop
//      is a permanently stale screen.
#pragma once

#include <Arduino.h>

namespace sonos {

#ifdef GENA_EVENTS

// Start the callback listener. Safe to call from boardInit()/appBoot() before Wi-Fi is up — the
// task waits for the link itself, the same way the sleep-machine's media httpd does, because
// appBoot() connects later than boardInit() runs.
void genaBegin(uint16_t port = 3401);

// Point the subscriptions at this coordinator. Call on boot and whenever the coordinator moves —
// grouping changes move it, and processPending() already re-discovers and bumps g_zonesGen at
// exactly those points. Passing the same IP twice is cheap and does nothing.
void genaSetCoordinator(const String &coordIp);

// netTask ONLY (it makes blocking HTTP calls). Drives (re)subscribe and renewal; self-rate-limited,
// so it is safe to call every loop.
void genaTick();

// Diagnostics for the health JSON — this is how you tell "eventing is working" from "eventing
// silently stopped and the backstop poll is carrying the screen".
struct GenaDiag {
  bool     subscribed;      // both services currently have a live SID
  uint32_t events;          // NOTIFYs accepted since boot
  uint32_t lastEventAgeMs;  // since the last accepted NOTIFY (UINT32_MAX = none yet)
  uint32_t renewals;        // successful renewals
  uint32_t resubscribes;    // renewals that failed and forced a fresh SUBSCRIBE (412 etc.)
  uint32_t failures;        // SUBSCRIBE attempts that failed outright
  uint16_t port;
};
void genaDiag(GenaDiag &out);

#else  // ------------------------------------------------------------------------ disabled

inline void genaBegin(uint16_t = 3401) {}
inline void genaSetCoordinator(const String &) {}
inline void genaTick() {}
struct GenaDiag {
  bool     subscribed = false;
  uint32_t events = 0, lastEventAgeMs = UINT32_MAX, renewals = 0, resubscribes = 0, failures = 0;
  uint16_t port = 0;
};
inline void genaDiag(GenaDiag &out) { out = GenaDiag(); }

#endif

}  // namespace sonos
