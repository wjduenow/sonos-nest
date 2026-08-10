// DIDL-Lite parsing for ContentDirectory Browse results. See plan §3 (hard part #1).
//
// RAM-constrained: lightweight string-scan for the few tags we need, NOT a full DOM.
#pragma once

#include <Arduino.h>
#include <vector>
#include "../player_state.h"

namespace sonos {

struct DidlItem {
  String id;          // object id (e.g. SQ:3)
  String title;       // dc:title
  String resUri;      // <res> stream/container URI
  String metadata;    // the item's own DIDL-Lite snippet (needed for SetAVTransportURI)
  bool   isContainer = false;
};

// Unescape XML entities (&lt; &gt; &amp; &quot; &apos;). Sonos escapes DIDL when embedding it in a
// SOAP response, and escapes it AGAIN inside a GENA event — so callers routinely need two passes.
String xmlUnescape(const String& in);

// Scan a DIDL-Lite XML payload into items. Returns count parsed.
size_t parseDidl(const String& didlXml, std::vector<DidlItem>& out);

// Parse the (XML-escaped) TrackMetaData from GetPositionInfo into now-playing fields
// (title/artist/album/artUri). artUri is left relative — caller prepends the speaker base.
void parseNowPlaying(const String& trackMetaData, PlayerState& out);

// *** CALL THIS ON ANY artUri YOU GET OUT OF parseNowPlaying(). ***
// Sonos reports <upnp:albumArtURI> RELATIVE — "/getaa?s=1&u=..." — which is not a usable URL.
// HTTPClient::begin() simply returns false on it, with no log and no error, so the art silently
// never loads. getPositionInfo() has always fixed this up inline; when the GENA path started
// calling parseNowPlaying() directly it did not, and the result was artwork that vanished and
// reappeared as event-sourced (relative, unusable) and poll-sourced (absolute, working) URLs took
// turns. Shared here so the next caller cannot miss it.
void artUriAbsolute(String& artUri, const String& speakerIp);

}  // namespace sonos
