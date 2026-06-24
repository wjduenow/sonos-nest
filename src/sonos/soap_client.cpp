#include "soap_client.h"
#include <HTTPClient.h>

// TODO Phase 1: implement soapAction() — build the SOAP envelope, set the SOAPACTION
//               header, POST via HTTPClient, return the response body.
// TODO Phase 1: play/pause/getTransportInfo against a hardcoded zone IP (SONOS_ZONE_IP).
// TODO Phase 2: getPositionInfo/getVolume/setVolume.
// TODO Phase 3: browse() + the playlist enqueue helpers.
//
// Dev affordance (plan §7): a serial SOAP tracer that logs request/response envelopes.

namespace sonos {

bool soapAction(const String&, const String&, const String&,
                const String&, const String&, String&) {
  return false;  // TODO
}

bool play(const String&)                                    { return false; }
bool pause(const String&)                                   { return false; }
bool next(const String&)                                    { return false; }
bool previous(const String&)                                { return false; }
bool seekTrack(const String&, uint32_t)                     { return false; }
bool setAvTransportUri(const String&, const String&, const String&) { return false; }
bool getTransportInfo(const String&, TransportState&)       { return false; }
bool getPositionInfo(const String&, PlayerState&)           { return false; }
bool getVolume(const String&, uint8_t&)                     { return false; }
bool setVolume(const String&, uint8_t)                      { return false; }
bool setMute(const String&, bool)                           { return false; }
bool browse(const String&, const String&, String&)         { return false; }
bool removeAllTracksFromQueue(const String&)               { return false; }
bool addUriToQueue(const String&, const String&, const String&) { return false; }

}  // namespace sonos
