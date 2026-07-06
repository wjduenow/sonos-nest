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

// Scan a DIDL-Lite XML payload into items. Returns count parsed.
size_t parseDidl(const String& didlXml, std::vector<DidlItem>& out);

// Parse the (XML-escaped) TrackMetaData from GetPositionInfo into now-playing fields
// (title/artist/album/artUri). artUri is left relative — caller prepends the speaker base.
void parseNowPlaying(const String& trackMetaData, PlayerState& out);

}  // namespace sonos
