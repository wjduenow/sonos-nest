#include "soap_client.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

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

bool soapAction(const String &ip, const String &controlPath, const String &service,
                const String &action, const String &bodyArgs, String &responseOut) {
  WiFiClient client;
  HTTPClient http;
  String url = "http://" + ip + ":1400" + controlPath;
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
  http.addHeader("SOAPACTION", "\"" + service + "#" + action + "\"");

  String body =
      "<?xml version=\"1.0\"?>"
      "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
      "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
      "<u:" + action + " xmlns:u=\"" + service + "\">" + bodyArgs +
      "</u:" + action + "></s:Body></s:Envelope>";

  if (SONOS_SOAP_TRACE) {
    Serial.printf("[soap] POST %s  action=%s\n", url.c_str(), action.c_str());
    Serial.println(body);
  }
  int code = http.POST(body);
  responseOut = http.getString();
  if (SONOS_SOAP_TRACE) {
    Serial.printf("[soap] <- %d (%d bytes)\n", code, responseOut.length());
    if (code != 200) Serial.println(responseOut);
  }
  http.end();
  return code == 200;
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
bool setAvTransportUri(const String &ip, const String &uri, const String &didlMeta) {
  String r;
  String args = "<InstanceID>0</InstanceID><CurrentURI>" + xmlEscape(uri) +
                "</CurrentURI><CurrentURIMetaData>" + xmlEscape(didlMeta) +
                "</CurrentURIMetaData>";
  return soapAction(ip, PATH_AVT, SVC_AVT, "SetAVTransportURI", args, r);
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
bool getPositionInfo(const String &ip, PlayerState &out) {
  String r;
  if (!soapAction(ip, PATH_AVT, SVC_AVT, "GetPositionInfo",
                  "<InstanceID>0</InstanceID>", r)) return false;
  out.durationSec = hmsToSec(extractTag(r, "TrackDuration"));
  out.positionSec = hmsToSec(extractTag(r, "RelTime"));
  // TrackMetaData is escaped DIDL-Lite; title/artist/art are parsed in didl.cpp (Phase 2).
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
bool browse(const String &ip, const String &objectId, String &didlOut) {
  String r;
  String args = "<ObjectID>" + xmlEscape(objectId) +
                "</ObjectID><BrowseFlag>BrowseDirectChildren</BrowseFlag><Filter>*</Filter>"
                "<StartingIndex>0</StartingIndex><RequestedCount>100</RequestedCount>"
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
