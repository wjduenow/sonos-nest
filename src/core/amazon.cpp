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
//  5. THE LINK CEREMONY IS AppLink, NOT DeviceLink — Amazon moved, and the old call is now an
//     ERROR rather than a deprecation. getDeviceLinkCode answers 500 Server.ServiceUnknownError
//     "Cannot parse null string" even with a valid household id; getAppLink answers 200 with the
//     Login-with-Amazon URL. Same ceremony otherwise, one difference: getAppLink does NOT mint a
//     linkDeviceId, so we pick our own. Run-verified 2026-09-04; existing tokens were unaffected,
//     which is why this broke silently — only NEW links failed.
#include "amazon.h"

#include "smapi.h"
#include "settings.h"
#include "sonos/soap_client.h"
#include "sonos/ssdp.h"
#include "net/wifi.h"     // wifiHostname — the only per-device name we have, used as our linkDeviceId
#include "net/logmirror.h"   // LOG — tees to the TCP mirror where enabled, plain Serial otherwise
#include "heap_watch.h"   // heapwatch::note — attribute the heap low-water

namespace amazon {

const char *kStationsRoot = "catalog/stations/#prime_stations";

static const char *kHost    = "sonos.amazonmusic.com";
static const char *kNs      = "http://www.sonos.com/Services/1.1";
static const int   kSid     = 201;
static const int   kType    = 51463;                        // kSid * 256 + 7
static const char *kDesc    = "SA_RINCON51463_X_#Svc51463-0-Token";

// Link state lives only for the duration of the ceremony; the resulting token goes to NVS.
static String s_linkCode, s_linkDeviceId;

// The XML helpers and the SOAP transport moved to core/smapi.{h,cpp} when Spotify needed the same
// ones — every comment explaining why each is shaped the way it is went with them. These aliases
// keep the call sites below reading as they always did.
using smapi::escapeXml;
using smapi::tagValue;
using smapi::unescapeXml;
using smapi::urlEncode;

// --- transport ----------------------------------------------------------------------------------
// One keep-alive TLS session to Amazon's SMAPI endpoint. Amazon posts to "/" — Spotify does not,
// which is why the path is a Client parameter rather than baked into the request line.
static smapi::Client s_client(kHost, "/", "amazon");

static String post(const String &action, const String &header, const String &body) {
  return s_client.post(action, header, body);
}

void endSession() { s_client.endSession(); }

static String credsHeader() {
  return smapi::loginCreds(settingsAmazonToken(), settingsAmazonKey(), settingsHouseholdId());
}

// An authenticated request, with the one recovery that matters: an expired token comes back as a
// fault CARRYING its own replacement, so extract, persist, and retry exactly once.
static String request(const String &action, const String &body) {
  String r = post(action, credsHeader(), body);
  if (r.indexOf("TokenRefreshRequired") < 0) return r;

  const String tok = unescapeXml(tagValue(r, "authToken"));
  const String key = unescapeXml(tagValue(r, "privateKey"));
  if (tok.length() == 0) {
    LOG.println("[amazon] token expired and the fault carried no replacement — re-link needed");
    return r;
  }
  settingsSetAmazonAuth(tok, key);
  LOG.printf("[amazon] token refreshed in-band (%u/%u chars), retrying\n",
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
      if (hh.length()) { settingsSetHouseholdId(hh); LOG.printf("[amazon] household %s\n", hh.c_str()); }
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
        if (v) { settingsSetAmazonSerial(v); LOG.printf("[amazon] account serial sn=%u\n", v); }
      }
    }
  }
}

// --- linking ------------------------------------------------------------------------------------

bool linked() { return settingsAmazonToken().length() > 0; }
void unlink()  { settingsSetAmazonAuth("", ""); s_linkCode = ""; s_linkDeviceId = ""; }

// Our own device identity for the ceremony, NOT Amazon's. DeviceLink used to mint a linkDeviceId
// and require it back; AppLink does not, so the client picks one and sends it on every poll. The
// hostname is the only per-device name this firmware has (the user sets it, and the portal keys on
// it too), and it is stable across reboots — which is what matters if a re-link ever has to present
// the same identity.
static String linkDeviceId() {
  const String h = wifiHostname();
  return h.length() ? h : String("sonos-nest");
}

// See item 5 in the file header: Amazon moved DeviceLink -> AppLink and getDeviceLinkCode is now a
// server error, so this asks for an app link instead. Everything downstream is unchanged.
bool linkBegin(String &regUrlOut) {
  const String hh = settingsHouseholdId();
  String body = String("<getAppLink xmlns=\"") + kNs + "\"><householdId>" +
                escapeXml(hh) + "</householdId></getAppLink>";
  String r = post("getAppLink", smapi::anonCreds(), body);
  regUrlOut      = unescapeXml(tagValue(r, "regUrl"));
  s_linkCode     = unescapeXml(tagValue(r, "linkCode"));
  s_linkDeviceId = linkDeviceId();
  if (regUrlOut.length() == 0 || s_linkCode.length() == 0) {
    const String fault = unescapeXml(tagValue(r, "faultstring"));
    LOG.printf("[amazon] link begin failed (%s)\n",
               fault.length() ? fault.c_str() : "no regUrl/linkCode in the response");
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
  String r = post("getDeviceAuthToken", smapi::anonCreds(), body);
  const String tok = unescapeXml(tagValue(r, "authToken"));
  if (tok.length() == 0) return false;      // NOT_LINKED_RETRY while the owner is still approving
  settingsSetAmazonAuth(tok, unescapeXml(tagValue(r, "privateKey")));
  s_linkCode = ""; s_linkDeviceId = "";
  LOG.println("[amazon] linked");
  return true;
}

// --- the ceremony, on its own task ---------------------------------------------------------------
// Sonos's own app polls getDeviceAuthToken for up to seven minutes, so that is the window used
// here. Polling every 3 s is cheap and makes the screen feel responsive when the owner approves.
static volatile LinkState s_state = LinkState::Idle;
static String   s_regUrl;
static volatile uint16_t s_left = 0;
static TaskHandle_t s_linkTask = nullptr;

LinkState linkState()        { return s_state; }
String    linkUrl()          { return s_regUrl; }
uint16_t  linkSecondsLeft()  { return s_left; }
void      linkCancel()       { if (s_state == LinkState::Waiting || s_state == LinkState::Starting)
                                 s_state = LinkState::Idle; }

static void linkTask(void *) {
  for (;;) {
    if (s_state != LinkState::Starting) { vTaskDelay(pdMS_TO_TICKS(250)); continue; }

    adopt();                          // the household id is required to ask for a code
    String url;
    if (!linkBegin(url)) {
      s_state = LinkState::Failed;
      continue;
    }
    s_regUrl = url;
    s_left   = 420;                   // seven minutes, matching the official app
    s_state  = LinkState::Waiting;

    while (s_state == LinkState::Waiting && s_left > 0) {
      vTaskDelay(pdMS_TO_TICKS(3000));
      s_left = (s_left > 3) ? (uint16_t)(s_left - 3) : 0;
      if (s_state != LinkState::Waiting) break;      // cancelled from the UI
      if (linkPoll()) { s_state = LinkState::Linked; break; }
    }
    if (s_state == LinkState::Waiting) s_state = LinkState::Failed;   // window expired
  }
}

void linkStart() {
  if (s_state == LinkState::Starting || s_state == LinkState::Waiting) return;
  s_regUrl = ""; s_left = 0;
  s_state = LinkState::Starting;
  // Core 0 with the network. Created lazily so a device that never links never spawns it.
  if (!s_linkTask) xTaskCreatePinnedToCore(linkTask, "amzlink", 6144, nullptr, 1, &s_linkTask, 0);
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

// The station root has held two different shapes, and this has to read both. Until 2026-09 it was
// 26 genre CONTAINERS, each browsing to ~50 stations; it is now a FLAT list of up to 100 playable
// stations (itemType=program, canEnumerate=false) and every refinements/genres id faults. Taking a
// program row as a genre is what produced 100 "genres" that each browse to nothing — see the guard
// at the end of radio_cache.cpp:refresh() for what that cost.
bool genres(std::vector<Genre> &out) {
  if (!linked()) return false;
  const String r = browse(kStationsRoot, 0, 100);   // server caps at 100; asking for more is free
  std::vector<String> blocks;
  eachCollection(r, blocks);
  int programs = 0;
  for (const String &b : blocks) {
    if (tagValue(b, "itemType") == "program") { ++programs; continue; }   // a station, not a level
    Genre g;
    g.title = unescapeXml(tagValue(b, "title"));
    g.id    = unescapeXml(tagValue(b, "id"));
    if (g.title.length() && g.id.length()) out.push_back(g);
  }
  // Flat root: hand back ONE implicit container so the cache, the crawl and the UI all keep their
  // shape. stations() filters on itemType=program, so browsing kStationsRoot through it returns
  // exactly the rows skipped above. If Amazon ever restores real containers, the loop above finds
  // them and this never fires — no flag, no setting, no migration.
  if (out.empty() && programs) {
    Genre g; g.title = "Stations"; g.id = kStationsRoot;
    out.push_back(g);
  }
  return !out.empty();
}

bool stations(const String &genreId, std::vector<Station> &out) {
  if (!linked()) return false;
  const String r = browse(genreId, 0, 100);  // a genre caps at ~50, the flat root at 100; no paging
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
