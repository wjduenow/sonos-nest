#include "soap_client.h"
#include "didl.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include "../net/logmirror.h"   // LOG — tees to the TCP mirror where enabled, plain Serial otherwise

// Set -DSONOS_SOAP_TRACE=1 to log every SOAP request/response over serial (plan §7 dev
// affordance). Off by default.
#ifndef SONOS_SOAP_TRACE
#define SONOS_SOAP_TRACE 0
#endif

namespace sonos {

static const char *SVC_AVT  = "urn:schemas-upnp-org:service:AVTransport:1";
static const char *SVC_RC   = "urn:schemas-upnp-org:service:RenderingControl:1";
static const char *SVC_CD   = "urn:schemas-upnp-org:service:ContentDirectory:1";
static const char *PATH_AVT = "/MediaRenderer/AVTransport/Control";
static const char *PATH_RC  = "/MediaRenderer/RenderingControl/Control";
static const char *PATH_CD  = "/MediaServer/ContentDirectory/Control";

// XML-escape a value being embedded in a SOAP arg (needed for URIs / DIDL metadata).
static String xmlEscape(const String &in) {
  String o;
  o.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in[i];
    switch (c) {
      case '&':  o += "&amp;";  break;
      case '<':  o += "&lt;";   break;
      case '>':  o += "&gt;";   break;
      case '"':  o += "&quot;"; break;
      case '\'': o += "&apos;"; break;
      default:   o += c;        break;
    }
  }
  return o;
}

// Extract the text content of the first <tag>...</tag> (no-attribute open tag).
static String extractTag(const String &xml, const char *tag) {
  String open = String("<") + tag + ">";
  String close = String("</") + tag + ">";
  int a = xml.indexOf(open);
  if (a < 0) return "";
  a += open.length();
  int b = xml.indexOf(close, a);
  if (b < 0) return "";
  return xml.substring(a, b);
}

// "H:MM:SS" -> seconds.
static uint32_t hmsToSec(const String &t) {
  int c1 = t.indexOf(':');
  if (c1 < 0) return 0;
  int c2 = t.indexOf(':', c1 + 1);
  if (c2 < 0) return 0;
  long h = t.substring(0, c1).toInt();
  long m = t.substring(c1 + 1, c2).toInt();
  long s = t.substring(c2 + 1).toInt();
  return (uint32_t)(h * 3600 + m * 60 + s);
}

// Runtime diagnostics for the "button gets slower the longer it runs" investigation, surfaced on
// the config page (webConfigJson). soapReconnects climbing over uptime = stale sockets being
// dropped/retried; soapMaxMs = worst single call.
static uint32_t s_soapCalls = 0, s_soapReconnects = 0, s_soapLastMs = 0, s_soapMaxMs = 0;
void soapDiag(uint32_t &calls, uint32_t &reconnects, uint32_t &lastMs, uint32_t &maxMs) {
  calls = s_soapCalls; reconnects = s_soapReconnects; lastMs = s_soapLastMs; maxMs = s_soapMaxMs;
}

bool soapAction(const String &ip, const String &controlPath, const String &service,
                const String &action, const String &bodyArgs, String &responseOut) {
  // Persistent client/connection: with setReuse(true), consecutive calls to the same host
  // reuse the open TCP socket instead of reconnecting — much lower latency for the common
  // case (poll + commands all hitting the coordinator). soapAction is only ever called from
  // netTask, so a static client is single-threaded and safe. (Album art uses its own client.)
  static WiFiClient client;
  static HTTPClient http;
  String url = "http://" + ip + ":1400" + controlPath;
  String body =
      "<?xml version=\"1.0\"?>"
      "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
      "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
      "<u:" + action + " xmlns:u=\"" + service + "\">" + bodyArgs +
      "</u:" + action + "></s:Body></s:Envelope>";

  if (SONOS_SOAP_TRACE) {
    LOG.printf("[soap] POST %s  action=%s\n", url.c_str(), action.c_str());
    LOG.println(body);
  }

  // Try the kept-alive socket; if it's stale the POST returns a negative code. Sonos closes idle
  // keep-alives server-side, which leaves our socket half-open — and setReuse(true) never stops
  // it, so without the client.stop() below those dead sockets pile up in CLOSE_WAIT until LWIP
  // runs out and every call blocks. That is what made the headless button get "log-jammed" and
  // slower the longer it ran; a reboot cleared the socket table. Closing on failure fixes the
  // leak; the single retry means a stale socket costs a reconnect, not a dropped command.
  for (int attempt = 0; attempt < 2; ++attempt) {
    http.setReuse(true);
    http.setConnectTimeout(3000);       // bound a dead-socket connect instead of hanging ~5 s
    if (!http.begin(client, url)) { client.stop(); s_soapReconnects++; continue; }
    http.setTimeout(4000);
    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    http.addHeader("SOAPACTION", "\"" + service + "#" + action + "\"");

    const uint32_t t0 = millis();
    int code = http.POST(body);
    responseOut = http.getString();
    const uint32_t dt = millis() - t0;
    http.end();

    s_soapCalls++;
    s_soapLastMs = dt;
    if (dt > s_soapMaxMs) s_soapMaxMs = dt;

    if (code > 0) {
      if (SONOS_SOAP_TRACE) {
        LOG.printf("[soap] <- %d (%d bytes)\n", code, responseOut.length());
        if (code != 200) LOG.println(responseOut);
      }
      return code == 200;
    }
    // Transport-level failure (likely a stale reused socket): drop it so it can't accumulate or
    // be reused again, and retry once on a fresh connection.
    s_soapReconnects++;
    client.stop();
  }
  return false;
}

// --- AVTransport ---
bool play(const String &ip) {
  String r;
  return soapAction(ip, PATH_AVT, SVC_AVT, "Play",
                    "<InstanceID>0</InstanceID><Speed>1</Speed>", r);
}
bool pause(const String &ip) {
  String r;
  return soapAction(ip, PATH_AVT, SVC_AVT, "Pause", "<InstanceID>0</InstanceID>", r);
}
bool next(const String &ip) {
  String r;
  return soapAction(ip, PATH_AVT, SVC_AVT, "Next", "<InstanceID>0</InstanceID>", r);
}
bool previous(const String &ip) {
  String r;
  return soapAction(ip, PATH_AVT, SVC_AVT, "Previous", "<InstanceID>0</InstanceID>", r);
}
bool seekTrack(const String &ip, uint32_t trackNr) {
  String r;
  String args = "<InstanceID>0</InstanceID><Unit>TRACK_NR</Unit><Target>" +
                String(trackNr) + "</Target>";
  return soapAction(ip, PATH_AVT, SVC_AVT, "Seek", args, r);
}
bool seekToStart(const String &ip) {
  String r;
  return soapAction(ip, PATH_AVT, SVC_AVT, "Seek",
                    "<InstanceID>0</InstanceID><Unit>REL_TIME</Unit><Target>00:00:00</Target>", r);
}
bool setAvTransportUri(const String &ip, const String &uri, const String &didlMeta) {
  String r;
  String args = "<InstanceID>0</InstanceID><CurrentURI>" + xmlEscape(uri) +
                "</CurrentURI><CurrentURIMetaData>" + xmlEscape(didlMeta) +
                "</CurrentURIMetaData>";
  return soapAction(ip, PATH_AVT, SVC_AVT, "SetAVTransportURI", args, r);
}
bool setPlayMode(const String &ip, const String &mode) {
  String r;  // NORMAL | REPEAT_ALL | REPEAT_ONE | SHUFFLE...
  String args = "<InstanceID>0</InstanceID><NewPlayMode>" + mode + "</NewPlayMode>";
  return soapAction(ip, PATH_AVT, SVC_AVT, "SetPlayMode", args, r);
}
bool getTransportInfo(const String &ip, TransportState &out) {
  String r;
  out = TransportState::Unknown;
  if (!soapAction(ip, PATH_AVT, SVC_AVT, "GetTransportInfo",
                  "<InstanceID>0</InstanceID>", r)) return false;
  String s = extractTag(r, "CurrentTransportState");
  if (s == "PLAYING")              out = TransportState::Playing;
  else if (s == "PAUSED_PLAYBACK") out = TransportState::Paused;
  else if (s == "STOPPED")         out = TransportState::Stopped;
  else if (s == "TRANSITIONING")   out = TransportState::Transitioning;
  return true;
}
bool getMediaInfo(const String &ip, String &currentUriOut, String *currentUriMetaOut) {
  String r;
  currentUriOut = "";
  if (currentUriMetaOut) *currentUriMetaOut = "";
  if (!soapAction(ip, PATH_AVT, SVC_AVT, "GetMediaInfo",
                  "<InstanceID>0</InstanceID>", r)) return false;
  currentUriOut = extractTag(r, "CurrentURI");
  // CurrentURIMetaData is where the title lives for content playing OUTSIDE the queue — a direct
  // Spotify track, for instance, whose GetPositionInfo TrackMetaData is a stub with item id="-1",
  // no dc:title, no dc:creator and no art. Verified on hardware: TrackMetaData empty while this
  // carried dc:title "Apologize".
  //
  // *** IT CARRIES ONE MORE ESCAPE LAYER THAN TrackMetaData. *** On the wire this field is
  // `&amp;lt;DIDL-Lite` where TrackMetaData is `&lt;DIDL-Lite` — escaped twice, not once. Handing
  // it straight to parseNowPlaying() (which unescapes once, then unescapes each field again) finds
  // no <dc:title> at all and silently returns nothing: no error, just a permanently blank screen.
  // Unescaping once here normalises it to TrackMetaData's depth, so every caller can treat the two
  // identically. Do not "simplify" this away.
  if (currentUriMetaOut) *currentUriMetaOut = xmlUnescape(extractTag(r, "CurrentURIMetaData"));
  return true;
}
bool becomeStandalone(const String &ip) {
  String r;
  return soapAction(ip, PATH_AVT, SVC_AVT, "BecomeCoordinatorOfStandaloneGroup",
                    "<InstanceID>0</InstanceID>", r);
}
bool getPositionInfo(const String &ip, PlayerState &out) {
  String r;
  if (!soapAction(ip, PATH_AVT, SVC_AVT, "GetPositionInfo",
                  "<InstanceID>0</InstanceID>", r)) return false;
  out.durationSec = hmsToSec(extractTag(r, "TrackDuration"));
  out.positionSec = hmsToSec(extractTag(r, "RelTime"));
  // TrackMetaData is escaped DIDL-Lite -> title/artist/album/art.
  parseNowPlaying(extractTag(r, "TrackMetaData"), out);
  // Album art URI is relative ("/getaa?...") and served by the speaker over plain HTTP.
  artUriAbsolute(out.artUri, ip);   // relative "/getaa?..." is unusable — see didl.h
  return true;
}

// --- RenderingControl (Channel=Master) ---
bool getVolume(const String &ip, uint8_t &out) {
  String r;
  if (!soapAction(ip, PATH_RC, SVC_RC, "GetVolume",
                  "<InstanceID>0</InstanceID><Channel>Master</Channel>", r)) return false;
  out = (uint8_t)extractTag(r, "CurrentVolume").toInt();
  return true;
}
bool setVolume(const String &ip, uint8_t vol) {
  String r;
  String args = "<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredVolume>" +
                String(vol) + "</DesiredVolume>";
  return soapAction(ip, PATH_RC, SVC_RC, "SetVolume", args, r);
}
bool setMute(const String &ip, bool mute) {
  String r;
  String args = "<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredMute>" +
                String(mute ? 1 : 0) + "</DesiredMute>";
  return soapAction(ip, PATH_RC, SVC_RC, "SetMute", args, r);
}

// --- ContentDirectory ---
bool browse(const String &ip, const String &objectId, String &didlOut,
            uint32_t startIndex, uint32_t count) {
  String r;
  String args = "<ObjectID>" + xmlEscape(objectId) +
                "</ObjectID><BrowseFlag>BrowseDirectChildren</BrowseFlag><Filter>*</Filter>"
                "<StartingIndex>" + String(startIndex) + "</StartingIndex>"
                "<RequestedCount>" + String(count) + "</RequestedCount>"
                "<SortCriteria></SortCriteria>";
  if (!soapAction(ip, PATH_CD, SVC_CD, "Browse", args, r)) return false;
  didlOut = extractTag(r, "Result");  // escaped DIDL-Lite; unescape/parse in didl.cpp
  return true;
}

// --- Queue helpers (Sonos playlist flow, plan §3) ---
bool removeAllTracksFromQueue(const String &ip) {
  String r;
  return soapAction(ip, PATH_AVT, SVC_AVT, "RemoveAllTracksFromQueue",
                    "<InstanceID>0</InstanceID>", r);
}
bool addUriToQueue(const String &ip, const String &uri, const String &didlMeta) {
  String r;
  String args = "<InstanceID>0</InstanceID><EnqueuedURI>" + xmlEscape(uri) +
                "</EnqueuedURI><EnqueuedURIMetaData>" + xmlEscape(didlMeta) +
                "</EnqueuedURIMetaData><DesiredFirstTrackNumberEnqueued>0"
                "</DesiredFirstTrackNumberEnqueued><EnqueueAsNext>0</EnqueueAsNext>";
  return soapAction(ip, PATH_AVT, SVC_AVT, "AddURIToQueue", args, r);
}

}  // namespace sonos
