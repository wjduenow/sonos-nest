// ONE INBOUND TRANSFER AT A TIME — the jukebox's ESP-Hosted link dies under inbound bursts, and this
// is the gate that keeps them from overlapping (issue #24, plans/13 candidate 4).
//
// The P4 talks Wi-Fi through an ESP32-C6 over SDIO, and once the C6's RX buffers fill with inbound
// TCP the host driver mishandles the backpressure and the link freezes (esp-hosted-mcu #184; open,
// no fix). Every death on record landed where two inbound transfers overlapped: a 15-29 KB SMAPI
// browse with tile fetches starting on top of it, or the Now Playing cover at play time under a
// browse. The individual transfers are all fine on their own — the crawl moves a megabyte a night
// one response at a time. So the rule is not "fetch less"; it is "never fetch two things at once".
//
// This replaced smapi::busy(): that only let TILES wait for a BROWSE, one direction of one pair.
// This serialises every large inbound reader in core — SMAPI (browse, search, link, the Amazon
// crawl), station tiles and Now Playing art — through one slot, whichever of them asks first.
//
// Not gated, on purpose: the Sonos SOAP poll (hundreds of bytes, and Now Playing must not lag a
// tile fetch), GENA NOTIFYs (we are the server), the portal registrar (a few hundred bytes), and
// the pull-OTA download (rare, and everything else is paused for it anyway).
//
// ⚠️ NOT RECURSIVE. A holder that calls into another gated path deadlocks itself for maxWaitMs and
// then proceeds ungated — the log says so, but do not rely on it. The one such path today is
// albumArtFetch() resolving a Spotify CDN URL (an SMAPI call) BEFORE it takes the gate for the
// body; keep that order if you touch it.
//
// A wait that times out PROCEEDS WITHOUT THE GATE rather than failing: a browse that fails because
// a tile fetch hung is worse for the user than a burst, and every holder here is bounded by its own
// HTTP timeout anyway. It is counted (`timeouts`) and read back in /api/config → health.inbound,
// so if it ever happens you will know without a serial cable.
#pragma once

#include <Arduino.h>

namespace inbound {

// Take the slot, waiting up to maxWaitMs. `tag` names the holder in logs and in health.inbound.
// Returns true when the slot is HELD and must be released; false when the wait timed out (proceed,
// but do not call release()).
bool acquire(const char *tag, uint32_t maxWaitMs);
void release();

// RAII form. `held` is public so a caller can release early — Now Playing art releases before it
// decodes, so a 45 ms progressive decode does not keep a browse waiting.
struct Guard {
  bool held;
  Guard(const char *tag, uint32_t maxWaitMs) : held(acquire(tag, maxWaitMs)) {}
  ~Guard() { release(); }
  void release() { if (held) { held = false; inbound::release(); } }
  Guard(const Guard &) = delete;
  Guard &operator=(const Guard &) = delete;
};

// Diagnostics for /api/config → health.inbound. Plain stores; a torn read skews a counter, nothing
// else. `holder` is "" when the slot is free; `heldMs` is how long the current holder has had it.
struct Stats {
  uint32_t    acquires;     // successful takes since boot
  uint32_t    waits;        // takes that had to wait at all
  uint32_t    timeouts;     // takes that gave up and proceeded ungated
  uint32_t    maxWaitMs;    // the longest anyone has waited
  uint32_t    maxHeldMs;    // the longest anyone has held it
  const char *maxHeldBy;    // ...and who
  const char *holder;
  uint32_t    heldMs;
};
void stats(Stats &out);

}  // namespace inbound
