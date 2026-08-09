// Sonos UPnP/SOAP client over plain HTTP to http://<zone-ip>:1400. See plan §3.
//
// ⚠️ Transport + queue actions must target the group COORDINATOR, not an arbitrary
// member. Resolve coordinator via topology before calling these.
#pragma once

#include <Arduino.h>
#include "../player_state.h"

namespace sonos {

// --- AVTransport: /MediaRenderer/AVTransport/Control ---
bool play(const String& ip);
bool pause(const String& ip);
bool next(const String& ip);
bool previous(const String& ip);
bool seekTrack(const String& ip, uint32_t trackNr);
bool setAvTransportUri(const String& ip, const String& uri, const String& didlMeta);
bool setPlayMode(const String& ip, const String& mode);   // NORMAL / REPEAT_ALL / REPEAT_ONE ...
bool getTransportInfo(const String& ip, TransportState& out);
bool getPositionInfo(const String& ip, PlayerState& out);   // track + pos + dur + DIDL
// The AVTransport source URI (GetMediaInfo/CurrentURI). Tells apart WHERE audio comes from:
// "x-rincon-queue:..." = the coordinator's own queue (what a saved-playlist play sets up),
// vs "x-sonos-htastream:" (TV), "x-rincon-stream:" (line-in), "x-rincon:" (grouped member), etc.
// currentUriMetaOut (optional) returns CurrentURIMetaData — the fallback source of now-playing
// title/artist when GetPositionInfo's TrackMetaData is a stub, which is what Sonos serves for
// content playing outside the queue (direct Spotify tracks). Escaped DIDL, same as TrackMetaData.
bool getMediaInfo(const String& ip, String& currentUriOut, String* currentUriMetaOut = nullptr);

// Grouping: join a speaker to a coordinator's group via
//   setAvTransportUri(memberIp, "x-rincon:" + coordinatorUuid, "")
// and ungroup (leave) a member with:
bool becomeStandalone(const String& ip);   // BecomeCoordinatorOfStandaloneGroup

// --- RenderingControl: /MediaRenderer/RenderingControl/Control (Channel=Master) ---
bool getVolume(const String& ip, uint8_t& out);
bool setVolume(const String& ip, uint8_t vol);
bool setMute(const String& ip, bool mute);

// --- ContentDirectory: /MediaServer/ContentDirectory/Control ---
// Returns raw DIDL-Lite XML for didl.h to parse. objectId e.g. "SQ:", "R:0", "FV:2".
// startIndex/count page the result — large lists (e.g. 69 favorites ≈ 116 KB) must be
// fetched in chunks so a single response never has to fit in the tight SRAM heap.
bool browse(const String& ip, const String& objectId, String& didlOut,
            uint32_t startIndex = 0, uint32_t count = 100);

// --- Playlist enqueue helpers (Flow B in plan §3) ---
bool removeAllTracksFromQueue(const String& ip);
bool addUriToQueue(const String& ip, const String& uri, const String& didlMeta);

// Low-level: POST a SOAP envelope. Logs request/response when tracing is on.
bool soapAction(const String& ip, const String& controlPath, const String& service,
                const String& action, const String& bodyArgs, String& responseOut);

// Runtime SOAP counters for diagnostics (surfaced on the config page): total calls, stale-socket
// reconnects (climbs if keep-alives are going bad), and last / worst-ever call time in ms.
void soapDiag(uint32_t& calls, uint32_t& reconnects, uint32_t& lastMs, uint32_t& maxMs);

}  // namespace sonos
