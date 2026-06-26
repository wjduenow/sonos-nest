// Async ContentDirectory browse + playback service. The UI (ui task) posts a browse or
// play request; netTask runs the SOAP off the UI thread and publishes results. Plan §3 §7.
#pragma once

#include <Arduino.h>
#include <vector>

namespace library {

// --- UI side ---
void requestBrowse(const String &objectId, bool queueFlow);  // SQ:=playlists, FV:2=favorites
bool busy();                                                 // a browse is in flight
bool takeResults(std::vector<String> &labelsOut);            // true once when new results land
void requestPlay(int index);                                 // play results[index]

// --- netTask side ---
void service(const String &browseIp, const String &coordIp, const String &coordUuid);

}  // namespace library
