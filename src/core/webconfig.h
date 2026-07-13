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

// The whole config document: current picks plus the choices available for each.
//   {"sleepTrack":"/Ocean.mp3","wakeTrack":"/Wake.mp3","room":"Nursery",
//    "tracks":[{"name":"Ocean","path":"/Ocean.mp3"}, ...],
//    "zones":[{"name":"Nursery","ip":"192.168.1.20"}, ...]}
// wakeTrack is "" when nothing is explicitly picked (the unit then auto-detects a "wake" file).
String webConfigJson();

// Apply one field: "sleepTrack" | "wakeTrack" | "room".
//   sleepTrack/wakeTrack — value is a track path; "" clears the pick (back to the default).
//   room                 — value is a zone name; persists it and asks netTask to switch.
// Returns false and fills err (a short human-readable reason) if the field or value is bad.
bool webConfigApply(const String &field, const String &value, String &err);

// Call when a track is removed from local storage: clears any pick that referenced it, so a
// deleted file can't leave the sleep/wake track pointing at something that no longer exists.
void webConfigTrackDeleted(const String &path);
