// Remote-config surface for a board's file-management web UI.
//
// Device-agnostic on purpose. This owns the knowledge of *what* is remotely configurable (the
// sleep track, the wake track, the Sonos room), where it's persisted (NVS via settings.*), and
// how a change is applied (the g_pending command channel, exactly as the on-device Settings
// screens do). A board's HTTP server does sockets, routing and nothing else — it must not reach
// up into core app state itself.
//
// The available choices come from the board HAL (localTrack*) and Sonos discovery, so a board
// with no local storage just reports an empty track list rather than needing a special case.
#pragma once

#include <Arduino.h>
#include <vector>

// The whole config document: current picks plus the choices available for each.
//   {"sleepTrack":"/Ocean.mp3","wakeTrack":"/Wake.mp3","room":"Nursery",
//    "tracks":[{"name":"Ocean","path":"/Ocean.mp3"}, ...],
//    "zones":[{"name":"Nursery","ip":"192.168.1.20"}, ...]}
// wakeTrack is "" when nothing is explicitly picked (the unit then auto-detects a "wake" file).
String webConfigJson();

// The device-identity payload for portal self-registration (registrar.cpp / plans/05). A focused
// sibling of webConfigJson() — no health/tracks/playlists, just who-and-where:
//   {"deviceName","mdnsName","ip","unit","board","fwVersion","configUrl","zones":[{name,ip}...]}
// unit/board are compile-time (the env's UNIT_*/HEADLESS macro); configUrl is boardConfigUrl()
// (the device's web config page; null only before WiFi is up).
String registrationJson();

// Apply one field: "sleepTrack" | "wakeTrack" | "room" | "ring" | "playlist" | "volume".
//   sleepTrack/wakeTrack — value is a track path; "" clears the pick (back to the default).
//   room                 — value is a zone name; persists it and asks netTask to switch.
//   ring                 — value is 0..100, the button-ring level (0 = off). Persisted only;
//                          the unit applies it (see webConfigGen below).
//   brightness           — value is 0..100, screen backlight % (nest); floored at 10. Persisted;
//                          the unit applies it via webConfigGen, same as ring.
//   playlist[2|3]        — value is a Sonos saved-playlist name (sonos-button). The bare name is
//                          the single press; 2 and 3 are the double and triple press. "" un-maps
//                          a double/triple press (refused for the single press — that one is the
//                          whole product).
//   volume[2|3]          — value is 0..100, the volume set before starting that press's playlist.
//   deviceName           — value is the DHCP hostname shown by the router. Sanitized to
//                          letters/digits/hyphen; reconnects so the new name registers.
// Returns false and fills err (a short human-readable reason) if the field or value is bad.
bool webConfigApply(const String &field, const String &value, String &err);

// The Sonos saved-playlist names, for the config page's dropdown.
//
// These can't be read synchronously the way zones() can: a playlist list is a ContentDirectory
// browse, which is async by design (library.h — the UI posts, netTask runs the SOAP). So the
// unit browses "SQ:" once its zone is up and publishes the labels here, and webConfigJson()
// serves the cache. Empty until that first browse lands, which the page handles.
void webConfigPlaylistsSet(const std::vector<String> &names);

// Bumped by webConfigApply() whenever it changes something a unit must act on *locally* (today:
// the ring level). A unit polls this in uiTick and re-reads the setting only when it moves.
//
// Why a counter and not just calling the board HAL from webConfigApply(): the ring is driven
// through backlightSet(), and on a board WITH a screen that call is the LCD backlight. Applying
// "ring" centrally would dim the sleep-machine's display. Letting the unit that owns a ring do
// the applying keeps the field meaningless on units that don't. Cheap: one int compare per tick
// versus hitting NVS.
uint32_t webConfigGen();

// Bumped ONLY when a playlist pick itself was edited — a strict subset of webConfigGen(), which
// also moves for volume, ring, brightness and the screensaver. The button uses it to decide when a
// re-warm must actually re-resolve: a playlist deleted and recreated under the same name needs a
// fresh browse, but paying that browse on every drag of a volume slider (which bumps the general
// counter several times a second) would put minutes of SOAP in front of the next press.
uint32_t webConfigPlaylistGen();

// Call when a track is removed from local storage: clears any pick that referenced it, so a
// deleted file can't leave the sleep/wake track pointing at something that no longer exists.
void webConfigTrackDeleted(const String &path);

// LVGL heap-pool usage, surfaced in the health readout so LV_MEM_SIZE can be right-sized against
// real peak usage instead of a guess. LVGL isn't thread-safe, so the unit samples lv_mem_monitor()
// on its OWN (UI) task and reports the numbers here; webConfigJson() serves the last sample. maxUsed
// is LVGL's own high-water since boot. All zero until the unit reports (headless boards never do).
void webConfigReportLvMem(uint32_t usedBytes, uint32_t maxUsedBytes, uint8_t fragPct);
