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

void parseNowPlaying(const String &trackMetaData, PlayerState &out) {
  out.title.clear();
  out.artist.clear();
  out.album.clear();
  out.artUri.clear();
  if (trackMetaData.length() == 0 || trackMetaData == "NOT_IMPLEMENTED") return;

  String d = xmlUnescape(trackMetaData);
  out.title  = between(d, "<dc:title>", "</dc:title>");
  out.artist = between(d, "<dc:creator>", "</dc:creator>");
  out.album  = between(d, "<upnp:album>", "</upnp:album>");
  out.artUri = between(d, "<upnp:albumArtURI>", "</upnp:albumArtURI>");
  // Streaming sources (radio) often have no dc:creator; fall back to the stream title.
  if (out.title.length() == 0) out.title = between(d, "<r:streamContent>", "</r:streamContent>");
}

size_t parseDidl(const String &, std::vector<DidlItem> &out) {
  out.clear();
  return 0;  // TODO Phase 3: enumerate <item>/<container> blocks for browse lists.
}

}  // namespace sonos
