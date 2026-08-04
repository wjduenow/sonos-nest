// See amazon.h. SMAPI client for Amazon Music (service id 201, type 51463 = 201*256+7).
//
// Four things here are non-obvious and were each established by breaking something first. They are
// all in plans/08-music-service-integration.md with the evidence; the summary is:
//
//  1. XML PARSING MUST TOLERATE ATTRIBUTES, NAMESPACE PREFIXES AND NESTING. Three separate false
//     negatives during research came from regexes that assumed a bare `<tag>`: albumArtURI always
//     carries `requiresAuthentication="false"`, the refresh fault uses `<ns:authToken>`, and a
//     favourite's `<r:resMD>` contains a nested `<item>` that a lazy match truncates at. Every one
//     produced a plausible wrong answer rather than an error. tagValue() below handles all three.
//  2. TOKEN REFRESH IS ONLY AVAILABLE IN-BAND. When the token expires the call fails with SOAP
//     fault Client.TokenRefreshRequired and the replacement credentials are inside the fault body.
//     Calling refreshAuthToken as an operation returns HTTP 404 "unsupported operation name" —
//     Amazon does not implement it, despite it being in the WSDL. So: parse the fault, persist,
//     retry once.
//  3. The token expires in WELL UNDER AN HOUR. Any long crawl will hit this mid-run, which is why
//     the retry is inside request() rather than left to callers.
//  4. Station ids carry a server-minted "#chunk-<uuid>" that must be stored verbatim. See amazon.h.
#include "amazon.h"

#include <WiFiClientSecure.h>

#include "settings.h"
#include "sonos/soap_client.h"
#include "sonos/ssdp.h"

namespace amazon {

const char *kStationsRoot = "catalog/stations/#prime_stations";

static const char *kHost    = "sonos.amazonmusic.com";
static const char *kNs      = "http://www.sonos.com/Services/1.1";
static const int   kSid     = 201;
static const int   kType    = 51463;                        // kSid * 256 + 7
static const char *kDesc    = "SA_RINCON51463_X_#Svc51463-0-Token";

// Link state lives only for the duration of the ceremony; the resulting token goes to NVS.
static String s_linkCode, s_linkDeviceId;

// --- XML helpers --------------------------------------------------------------------------------

// Value of the first <tag>...</tag>, matched on the BARE LOCAL NAME so that a namespace prefix
// (<ns:authToken>) and attributes (<albumArtURI requiresAuthentication="false">) both work. Both
// forms occur in real responses and both silently defeated a naive `<tag>` match during research.
// Returns "" when absent; callers that care must check emptiness rather than trusting a default.
static String tagValue(const String &xml, const char *tag, int from = 0) {
  const String want(tag);
  int p = from;
  while (p >= 0 && p < (int)xml.length()) {
    const int lt = xml.indexOf('<', p);
    if (lt < 0) break;
    const int gt = xml.indexOf('>', lt);
    if (gt < 0) break;
    String name = xml.substring(lt + 1, gt);
    if (name.startsWith("/") || name.startsWith("?") || name.startsWith("!")) { p = gt + 1; continue; }
    const int sp = name.indexOf(' ');
    if (sp >= 0) name = name.substring(0, sp);       // drop attributes
    if (name.endsWith("/")) { p = gt + 1; continue; } // self-closing: no value
    const int colon = name.indexOf(':');
    const String bare = (colon >= 0) ? name.substring(colon + 1) : name;
    if (bare == want) {
      const int e = xml.indexOf(String("</") + name + ">", gt + 1);
      if (e < 0) return "";
      return xml.substring(gt + 1, e);
    }
    p = gt + 1;
  }
  return "";
}

static String unescapeXml(String s) {
  s.replace("&lt;", "<");  s.replace("&gt;", ">");
  s.replace("&quot;", "\""); s.replace("&apos;", "'");
  s.replace("&amp;", "&");   // last, or the others double-decode
  return s;
}
static String escapeXml(const String &in) {
  String o; o.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    if      (c == '&')  o += "&amp;";
    else if (c == '<')  o += "&lt;";
    else if (c == '>')  o += "&gt;";
    else if (c == '"')  o += "&quot;";
    else o += c;
  }
  return o;
}

// SMAPI object ids go into a URI percent-encoded. '/' and '#' are the ones that matter.
static String urlEncode(const String &in) {
  static const char *hex = "0123456789abcdef";
  String o; o.reserve(in.length() * 2);
  for (size_t i = 0; i < in.length(); ++i) {
    const unsigned char c = (unsigned char)in[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { o += '%'; o += hex[c >> 4]; o += hex[c & 0xF]; }
  }
  return o;
}

// --- transport ----------------------------------------------------------------------------------

// One SOAP round trip. `header` is the full <s:Header> contents. Returns the body, or "" on
// transport failure. HTTP status is not distinguished: a SOAP fault arrives as a 500 with a body we
// still need to read, so the caller inspects the body either way.
static String post(const String &action, const String &header, const String &body) {
  WiFiClientSecure cli;
  cli.setInsecure();          // same posture as core/net/updater.cpp — no cert store on device
  cli.setTimeout(15000);
  if (!cli.connect(kHost, 443)) {
    Serial.println("[amazon] connect failed");
    return "";
  }
  String env = String("<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                      "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
                      "<s:Header>") + header + "</s:Header><s:Body>" + body +
               "</s:Body></s:Envelope>";
  String req = String("POST / HTTP/1.1\r\nHost: ") + kHost +
               "\r\nContent-Type: text/xml; charset=\"utf-8\""
               "\r\nSOAPAction: \"" + kNs + "#" + action + "\"" +
               "\r\nUser-Agent: Linux UPnP/1.0 Sonos/84.1-59230"
               "\r\nConnection: close\r\nContent-Length: " + String(env.length()) + "\r\n\r\n";
  cli.print(req);
  cli.print(env);

  // Skip headers, then read the body. Responses are small (root ~11 KB, a genre ~19 KB).
  uint32_t deadline = millis() + 20000;
  while (cli.connected() && millis() < deadline) {
    String line = cli.readStringUntil('\n');
    if (line == "\r" || line.length() == 0) break;
  }
  String out;
  out.reserve(24 * 1024);
  while (cli.connected() && millis() < deadline) {
    if (!cli.available()) { delay(5); continue; }
    out += (char)cli.read();
  }
  cli.stop();
  return out;
}

static String credsHeader() {
  return String("<credentials xmlns=\"") + kNs + "\"><deviceProvider>Sonos</deviceProvider>"
         "<loginToken><token>" + escapeXml(settingsAmazonToken()) +
         "</token><key>" + escapeXml(settingsAmazonKey()) +
         "</key><householdId>" + escapeXml(settingsHouseholdId()) +
         "</householdId></loginToken></credentials>";
}

// An authenticated request, with the one recovery that matters: an expired token comes back as a
// fault CARRYING its own replacement, so extract, persist, and retry exactly once.
static String request(const String &action, const String &body) {
  String r = post(action, credsHeader(), body);
  if (r.indexOf("TokenRefreshRequired") < 0) return r;

  const String tok = unescapeXml(tagValue(r, "authToken"));
  const String key = unescapeXml(tagValue(r, "privateKey"));
  if (tok.length() == 0) {
    Serial.println("[amazon] token expired and the fault carried no replacement — re-link needed");
    return r;
  }
  settingsSetAmazonAuth(tok, key);
  Serial.printf("[amazon] token refreshed in-band (%u/%u chars), retrying\n",
                (unsigned)tok.length(), (unsigned)key.length());
  return post(action, credsHeader(), body);
}

// --- household-derived values ---------------------------------------------------------------------

void adopt() {
  if (settingsHouseholdId().length() && settingsAmazonSerial()) return;
  std::vector<sonos::Zone> zs;
  sonos::zonesSnapshot(zs);
  if (zs.empty()) return;
  const String ip = zs[0].ip;

  if (settingsHouseholdId().isEmpty()) {
    String r;
    if (sonos::soapAction(ip, "/DeviceProperties/Control",
                   "urn:schemas-upnp-org:service:DeviceProperties:1", "GetHouseholdID", "", r)) {
      const String hh = tagValue(r, "CurrentHouseholdID");
      if (hh.length()) { settingsSetHouseholdId(hh); Serial.printf("[amazon] household %s\n", hh.c_str()); }
    }
  }

  // The sn= parameter. Our own account's serial is whatever the household already assigned to
  // Amazon Music, and the only place it is readable is an existing favourite's res URI. Playback
  // was verified with the correct value; sending 0 is untested, so prefer the real one and only
  // fall back if the household has no Amazon favourite at all.
  if (!settingsAmazonSerial()) {
    String r;
    if (sonos::soapAction(ip, "/MediaServer/ContentDirectory/Control",
                   "urn:schemas-upnp-org:service:ContentDirectory:1", "Browse",
                   "<ObjectID>FV:2</ObjectID><BrowseFlag>BrowseDirectChildren</BrowseFlag>"
                   "<Filter>*</Filter><StartingIndex>0</StartingIndex><RequestedCount>100</RequestedCount>"
                   "<SortCriteria></SortCriteria>", r)) {
      const int at = r.indexOf("sid=201");
      const int sn = (at < 0) ? -1 : r.indexOf("sn=", at);
      if (sn > 0) {
        const uint8_t v = (uint8_t)strtoul(r.substring(sn + 3, sn + 6).c_str(), nullptr, 10);
        if (v) { settingsSetAmazonSerial(v); Serial.printf("[amazon] account serial sn=%u\n", v); }
      }
    }
  }
}

// --- linking ------------------------------------------------------------------------------------

bool linked() { return settingsAmazonToken().length() > 0; }
void unlink()  { settingsSetAmazonAuth("", ""); s_linkCode = ""; s_linkDeviceId = ""; }

bool linkBegin(String &regUrlOut) {
  const String hh = settingsHouseholdId();
  String body = String("<getDeviceLinkCode xmlns=\"") + kNs + "\"><householdId>" +
                escapeXml(hh) + "</householdId></getDeviceLinkCode>";
  String r = post("getDeviceLinkCode",
                  String("<credentials xmlns=\"") + kNs +
                  "\"><deviceProvider>Sonos</deviceProvider></credentials>", body);
  regUrlOut      = unescapeXml(tagValue(r, "regUrl"));
  s_linkCode     = unescapeXml(tagValue(r, "linkCode"));
  s_linkDeviceId = unescapeXml(tagValue(r, "linkDeviceId"));
  // linkDeviceId is per-request AND required to redeem the code. Losing it makes a completed
  // authorisation unredeemable and forces the owner to approve a second time — which happened.
  if (regUrlOut.length() == 0 || s_linkCode.length() == 0 || s_linkDeviceId.length() == 0) {
    Serial.println("[amazon] link begin failed (missing regUrl/linkCode/linkDeviceId)");
    return false;
  }
  return true;
}

bool linkPoll() {
  if (s_linkCode.length() == 0) return false;
  String body = String("<getDeviceAuthToken xmlns=\"") + kNs + "\"><householdId>" +
                escapeXml(settingsHouseholdId()) + "</householdId><linkCode>" +
                escapeXml(s_linkCode) + "</linkCode><linkDeviceId>" +
                escapeXml(s_linkDeviceId) + "</linkDeviceId></getDeviceAuthToken>";
  String r = post("getDeviceAuthToken",
                  String("<credentials xmlns=\"") + kNs +
                  "\"><deviceProvider>Sonos</deviceProvider></credentials>", body);
  const String tok = unescapeXml(tagValue(r, "authToken"));
  if (tok.length() == 0) return false;      // NOT_LINKED_RETRY while the owner is still approving
  settingsSetAmazonAuth(tok, unescapeXml(tagValue(r, "privateKey")));
  s_linkCode = ""; s_linkDeviceId = "";
  Serial.println("[amazon] linked");
  return true;
}

// --- browsing -----------------------------------------------------------------------------------

// Walk <mediaCollection> blocks. Stations arrive as itemType=program inside mediaCollection —
// NOT mediaMetadata — which is its own small trap for anyone scoping a parser to the latter.
static void eachCollection(const String &xml, std::vector<String> &blocks) {
  int p = 0;
  while (true) {
    const int s = xml.indexOf("mediaCollection>", p);
    if (s < 0) break;
    const int open = xml.lastIndexOf('<', s);
    const int e = xml.indexOf("</", s);
    const int close = (e < 0) ? -1 : xml.indexOf("mediaCollection>", e);
    if (open < 0 || close < 0) break;
    blocks.push_back(xml.substring(open, close + 16));
    p = close + 16;
  }
}

static String browse(const String &objectId, int index, int count) {
  String body = String("<getMetadata xmlns=\"") + kNs + "\"><id>" + escapeXml(objectId) +
                "</id><index>" + String(index) + "</index><count>" + String(count) +
                "</count></getMetadata>";
  return request("getMetadata", body);
}

bool genres(std::vector<Genre> &out) {
  if (!linked()) return false;
  const String r = browse(kStationsRoot, 0, 60);
  std::vector<String> blocks;
  eachCollection(r, blocks);
  for (const String &b : blocks) {
    Genre g;
    g.title = unescapeXml(tagValue(b, "title"));
    g.id    = unescapeXml(tagValue(b, "id"));
    if (g.title.length() && g.id.length()) out.push_back(g);
  }
  return !out.empty();
}

bool stations(const String &genreId, std::vector<Station> &out) {
  if (!linked()) return false;
  const String r = browse(genreId, 0, 50);   // genres cap at 50; no paging needed
  std::vector<String> blocks;
  eachCollection(r, blocks);
  for (const String &b : blocks) {
    if (tagValue(b, "itemType") != "program") continue;
    Station s;
    s.title  = unescapeXml(tagValue(b, "title"));
    s.id     = unescapeXml(tagValue(b, "id"));
    s.artUrl = unescapeXml(tagValue(b, "albumArtURI"));
    if (s.title.length() && s.id.length()) out.push_back(s);
  }
  return !out.empty();
}

// --- playback + artwork -------------------------------------------------------------------------

String playUri(const Station &s) {
  return "x-sonosapi-radio:" + urlEncode(s.id) + "?sid=" + String(kSid) + "&flags=8300&sn=" +
         String(settingsAmazonSerial());
}

String playMeta(const Station &s, const String &genreId) {
  return String("<DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
                "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" "
                "xmlns:r=\"urn:schemas-rinconnetworks-com:metadata-1-0/\" "
                "xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">"
                "<item id=\"100c206c") + urlEncode(s.id) +
         "\" parentID=\"10082064" + urlEncode(genreId) + "\" restricted=\"true\">"
         "<dc:title>" + escapeXml(s.title) + "</dc:title>"
         "<upnp:class>object.item.audioItem.audioBroadcast</upnp:class>"
         "<desc id=\"cdudn\" nameSpace=\"urn:schemas-rinconnetworks-com:metadata-1-0/\">" +
         kDesc + "</desc></item></DIDL-Lite>";
}

String artThumbUrl(const String &artUrl, int px) {
  const int dot = artUrl.lastIndexOf('.');
  if (dot < 0) return artUrl;
  const String ext = artUrl.substring(dot + 1);
  if (!ext.equalsIgnoreCase("jpg") && !ext.equalsIgnoreCase("jpeg") && !ext.equalsIgnoreCase("png"))
    return artUrl;
  return artUrl.substring(0, dot) + "._SL" + String(px) + "_.jpg";
}

}  // namespace amazon
