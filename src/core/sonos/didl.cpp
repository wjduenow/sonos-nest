#include "didl.h"

namespace sonos {

// Unescape the XML entities Sonos uses when embedding DIDL inside a SOAP response.
// Exported (see didl.h) because GENA needs the identical pass: a NOTIFY body is escaped at the
// propertyset layer and each val="..." inside it is escaped AGAIN, which is the same
// double-escaping this file already exists to unpick.
String xmlUnescape(const String &in) {
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

void artUriAbsolute(String &artUri, const String &speakerIp) {
  if (artUri.length() == 0) return;

  // The common case: Sonos reports art relative to the speaker, which serves it over plain HTTP.
  if (artUri.startsWith("/")) {
    if (speakerIp.length()) artUri = "http://" + speakerIp + ":1400" + artUri;
    return;
  }
  if (artUri.startsWith("http://")) return;    // absolute and fetchable as-is

  // Anything left is a cloud service handing us an ABSOLUTE https:// image URL instead of a
  // speaker-relative one — Amazon Music does this (…media-amazon.com/images/I/…). Drop it.
  //
  // THIS IS A CRASH FIX, NOT A TIDY-UP. The art path (core/ui/album_art.cpp) fetches with a plain
  // WiFiClient, and against a TLS port that does not merely fail: HTTPClient reads response
  // headers with readStringUntil(), which is Stream::timedRead() — a tight `while (millis() -
  // start < _timeout) read();` spin with NO yield — and HTTPClient sets that timeout to its own
  // 5000 ms. artTask runs at priority 1 on core 0, above IDLE0 at priority 0, and this build has
  // CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y with CONFIG_ESP_TASK_WDT_TIMEOUT_S=5. So one
  // stalled header read starves IDLE0 for as long as the watchdog allows and the chip resets:
  // observed on the jukebox as repeated reboots with esp_reset_reason()==6 (ESP_RST_TASK_WDT),
  // roughly one per few hours of Amazon playback because artTask retries four times per track and
  // each retry is a fresh coin toss between the two 5 s timers.
  //
  // Clearing (rather than leaving it for the fetch to reject) also stops the retry churn: artTask
  // treats an empty URI as "nothing to show", clears immediately and stops, instead of burning
  // four 5 s attempts on every track change.
  //
  // The cost is that these tracks show no cover — which is what already happened, just after four
  // timeouts and a possible reboot. Actually RESTORING art for them is a separate job: the
  // speaker's /getaa proxy keys off the TRACK uri, which is not currently kept in PlayerState, and
  // fetching the https URL directly means a TLS client in shared core, whose buffers the two
  // ESP32-S3 units cannot afford (see CLAUDE.md on internal SRAM and `connection refused`).
  //
  // UNLESS this build can actually fetch it. ALBUM_ART_TLS gives album_art.cpp a TLS client and a
  // CDN-resize step, so on those units the URL is usable and clearing it would just throw the
  // cover away. The flag is per-env on purpose: mbedTLS allocates from PSRAM on the jukebox
  // (MBEDTLS_EXTERNAL_MEM_ALLOC in its custom_sdkconfig) but from INTERNAL SRAM on the two S3
  // screens, where a 16 KB in + 16 KB out pair is exactly the thing CLAUDE.md says surfaces as
  // Sonos "connection refused". So they keep dropping these, and keep showing no cover.
#ifndef ALBUM_ART_TLS
  artUri.clear();
#endif
}

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
    // Metadata for SetAVTransportURI / AddURIToQueue. Favorites (FV:2) wrap the real
    // playable object in <r:resMD>: its content carries the correct upnp:class AND the
    // <desc id="cdudn"> service-auth token. Passing the favorite wrapper instead (class
    // object.itemobject.item.sonos-favorite, no token) makes the speaker reject the source.
    // resMD is double-escaped here (one unescape happened on the Browse Result), so unescape
    // once more to recover real DIDL; soapAction re-escapes it for transport. Non-favorite
    // lists (playlists/queue) have no resMD — fall back to the item's own DIDL snippet.
    String resMD = between(block, "<r:resMD>", "</r:resMD>");
    item.metadata = resMD.length() ? xmlUnescape(resMD)
                                   : String(DIDL_HDR) + block + "</DIDL-Lite>";

    if (item.title.length()) out.push_back(item);
  }
  return out.size();
}

}  // namespace sonos
