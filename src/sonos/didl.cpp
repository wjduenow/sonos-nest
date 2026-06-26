#include "didl.h"

namespace sonos {

// Unescape the XML entities Sonos uses when embedding DIDL inside a SOAP response.
static String xmlUnescape(const String &in) {
  String o;
  o.reserve(in.length());
  for (size_t i = 0; i < in.length();) {
    if (in[i] == '&') {
      if      (in.startsWith("&lt;",   i)) { o += '<';  i += 4; }
      else if (in.startsWith("&gt;",   i)) { o += '>';  i += 4; }
      else if (in.startsWith("&amp;",  i)) { o += '&';  i += 5; }
      else if (in.startsWith("&quot;", i)) { o += '"';  i += 6; }
      else if (in.startsWith("&apos;", i)) { o += '\''; i += 6; }
      else { o += in[i]; ++i; }
    } else {
      o += in[i];
      ++i;
    }
  }
  return o;
}

static String between(const String &s, const char *openTag, const char *closeTag) {
  int a = s.indexOf(openTag);
  if (a < 0) return "";
  a += strlen(openTag);
  int b = s.indexOf(closeTag, a);
  if (b < 0) return "";
  return s.substring(a, b);
}

static String attrOf(const String &s, const char *name) {
  String key = String(name) + "=\"";
  int i = s.indexOf(key);
  if (i < 0) return "";
  i += key.length();
  int e = s.indexOf('"', i);
  return e < 0 ? "" : s.substring(i, e);
}

static const char *DIDL_HDR =
    "<DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
    "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" "
    "xmlns:r=\"urn:schemas-rinconnetworks-com:metadata-1-0/\" "
    "xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">";

void parseNowPlaying(const String &trackMetaData, PlayerState &out) {
  out.title.clear();
  out.artist.clear();
  out.album.clear();
  out.artUri.clear();
  if (trackMetaData.length() == 0 || trackMetaData == "NOT_IMPLEMENTED") return;

  // TrackMetaData is escaped twice: once for the SOAP response, and the DIDL field values
  // (notably the art URL's '&') are escaped again. Unescape the DIDL layer, then unescape
  // each extracted value to recover the real text/URL.
  String d = xmlUnescape(trackMetaData);
  out.title  = xmlUnescape(between(d, "<dc:title>", "</dc:title>"));
  out.artist = xmlUnescape(between(d, "<dc:creator>", "</dc:creator>"));
  out.album  = xmlUnescape(between(d, "<upnp:album>", "</upnp:album>"));
  out.artUri = xmlUnescape(between(d, "<upnp:albumArtURI>", "</upnp:albumArtURI>"));
  // Streaming sources (radio) often have no dc:creator; fall back to the stream title.
  if (out.title.length() == 0)
    out.title = xmlUnescape(between(d, "<r:streamContent>", "</r:streamContent>"));
}

size_t parseDidl(const String &didlEscaped, std::vector<DidlItem> &out) {
  out.clear();
  // The Browse Result is XML-escaped (like TrackMetaData); unescape once to real DIDL XML.
  String d = xmlUnescape(didlEscaped);

  int pos = 0;
  for (;;) {
    int it = d.indexOf("<item", pos);
    int ct = d.indexOf("<container", pos);
    if (it < 0 && ct < 0) break;
    bool container = (ct >= 0 && (it < 0 || ct < it));
    int start = container ? ct : it;
    const char *closeTag = container ? "</container>" : "</item>";
    int end = d.indexOf(closeTag, start);
    if (end < 0) break;
    end += strlen(closeTag);
    String block = d.substring(start, end);
    pos = end;

    DidlItem item;
    item.isContainer = container;
    item.id    = attrOf(block, "id");
    item.title = xmlUnescape(between(block, "<dc:title>", "</dc:title>"));

    int rs = block.indexOf("<res");
    if (rs >= 0) {
      int rgt = block.indexOf('>', rs);
      int re  = block.indexOf("</res>", rgt);
      if (rgt >= 0 && re > rgt) item.resUri = xmlUnescape(block.substring(rgt + 1, re));
    }
    // Metadata for SetAVTransportURI / AddURIToQueue: the item wrapped in a DIDL-Lite
    // envelope (kept singly-escaped; soapAction re-escapes it for transport).
    item.metadata = String(DIDL_HDR) + block + "</DIDL-Lite>";

    if (item.title.length()) out.push_back(item);
  }
  return out.size();
}

}  // namespace sonos
