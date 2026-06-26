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
bool getTransportInfo(const String& ip, TransportState& out);
bool getPositionInfo(const String& ip, PlayerState& out);   // track + pos + dur + DIDL

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
bool browse(const String& ip, const String& objectId, String& didlOut);

// --- Playlist enqueue helpers (Flow B in plan §3) ---
bool removeAllTracksFromQueue(const String& ip);
bool addUriToQueue(const String& ip, const String& uri, const String& didlMeta);

// Low-level: POST a SOAP envelope. Logs request/response when tracing is on.
bool soapAction(const String& ip, const String& controlPath, const String& service,
                const String& action, const String& bodyArgs, String& responseOut);

}  // namespace sonos
