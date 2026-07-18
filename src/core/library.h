// Async ContentDirectory browse + playback service. The UI (ui task) posts a browse or
// play request; netTask runs the SOAP off the UI thread and publishes results. Plan §3 §7.
#pragma once

#include <Arduino.h>
#include <vector>

namespace library {

// Play modes for the current browse list:
enum PlayMode { PLAY_FAVORITE = 0,  // SetAVTransportURI + Play   (FV:2)
                PLAY_PLAYLIST = 1,  // clear queue, enqueue, Play (SQ:)
                PLAY_QUEUE    = 2 };// Seek to track N in the queue (Q:0)

// --- UI side ---
void setLoopMode(bool on);           // if set, enqueued playlists play looped (REPEAT_ALL)
void requestBrowse(const String &objectId, int playMode);   // FV:2 / SQ: / Q:0
bool busy();                                                 // a browse is in flight
bool takeResults(std::vector<String> &labelsOut);            // true once when new results land
void requestPlay(int index);                                 // act on results[index] per mode
void clearResults();                                         // free cached items (on leaving)

// Play a saved playlist BY NAME, skipping the browse on the hot path. The first call for a name
// resolves it (browse SQ:+FV:2, match, re-fetch its DIDL) and caches the playable item; later
// calls with the same name enqueue + play straight from the cache — no ContentDirectory browse,
// which is where the multi-second press-to-audio latency lived. `warmOnly` resolves and caches
// without playing, to pre-warm at boot so even the first press is fast. Always enqueues looped
// per setLoopMode(). A name that doesn't resolve is reported via playNamedFailed().
void requestPlayNamed(const String &name, bool warmOnly = false);

// One-shot: true if the most recent requestPlayNamed() couldn't find the playlist (so the unit
// can log what's available). Cleared by reading it.
bool playNamedFailed();

// --- netTask side ---
void service(const String &browseIp, const String &coordIp, const String &coordUuid);

}  // namespace library
