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
void requestBrowse(const String &objectId, int playMode);   // FV:2 / SQ: / Q:0
bool busy();                                                 // a browse is in flight
bool takeResults(std::vector<String> &labelsOut);            // true once when new results land
void requestPlay(int index);                                 // act on results[index] per mode

// --- netTask side ---
void service(const String &browseIp, const String &coordIp, const String &coordUuid);

}  // namespace library
