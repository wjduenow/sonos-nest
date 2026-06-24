// DIDL-Lite parsing for ContentDirectory Browse results. See plan §3 (hard part #1).
//
// RAM-constrained: lightweight string-scan for the few tags we need, NOT a full DOM.
#pragma once

#include <Arduino.h>
#include <vector>

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

}  // namespace sonos
