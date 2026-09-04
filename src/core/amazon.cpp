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

#include <WiFiClientSecure.h>

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

// A SINGLE KEEP-ALIVE TLS SESSION, reused across every call.
//
// It used to open a fresh WiFiClientSecure per request and send `Connection: close`, so one crawl
// cost 27 TCP connects, 27 TLS handshakes and 27 closes. Handshakes measured 0.5-1.4 s each, so
// that alone was most of the crawl's wall clock — and that connect/close churn is the sustained
// load profile that wedges this board's ESP-Hosted link (plans/07). One session instead.
//
// Reuse forces us to know where a response ENDS, which read-to-EOF never had to. Content-Length is
// honoured and the socket kept; anything else (chunked, or no length at all) falls back to the old
// read-until-close behaviour and drops the session, so a server that will not do keep-alive is
// merely no worse than before. Deliberately no de-chunker: it needs a second copy of a ~19 KB body
// and the crawl runs with ~40 KB of internal heap free.
static WiFiClientSecure *s_cli = nullptr;
static uint32_t s_lastUseMs = 0;
static const uint32_t kIdleDropMs = 30000;   // a session idle this long is presumed dead

static void dropSession() {
  if (!s_cli) return;
  s_cli->stop();
  delete s_cli;
  s_cli = nullptr;
}

void endSession() { dropSession(); }

static bool ensureSession() {
  if (s_cli) {
    if (s_cli->connected() && (millis() - s_lastUseMs) < kIdleDropMs) return true;
    dropSession();
  }
  s_cli = new WiFiClientSecure();
  if (!s_cli) return false;
  s_cli->setInsecure();       // same posture as core/net/updater.cpp — no cert store on device
  s_cli->setTimeout(15000);
  if (!s_cli->connect(kHost, 443)) { dropSession(); return false; }
  return true;
}

// Reads one complete HTTP response. `keepAlive` reports whether the socket is still in a known
// state afterwards. False return = the response never arrived.
//
// NEVER use readStringUntil() or any Stream helper on a TLS socket here. Two compounding traps,
// which together rebooted this device in a loop:
//
//   * They read ONE BYTE per call, and on WiFiClientSecure every byte is a full mbedtls_ssl_read
//     plus an available() that polls the SSL record layer.
//   * Worse, Stream::timedRead() is `do { read(); } while (millis() - start < _timeout)` — a
//     BUSY-WAIT WITH NO YIELD. With a 15 s timeout, a header line whose next byte has not arrived
//     yet spins for fifteen seconds without letting another task run.
//
// On core 0 at priority 1 that starves IDLE0, so the task watchdog aborts the chip:
//   "IDLE0 (CPU 0) did not reset ... CPU 0: radiocache"  ->  SW_CPU_RESET, mid-crawl.
// Every individual phase measures under 1.5 s, so phase timing does NOT find it; the decoded
// backtrace does. Read blocks, and always yield.
static bool readResponse(String &out, bool &keepAlive) {
  const uint32_t deadline = millis() + 20000;
  uint8_t buf[1024];
  uint32_t lastYield = millis();
  keepAlive = false;

  String raw;
  raw.reserve(24 * 1024);
  auto pump = [&]() -> bool {                     // one block, yielding; false = socket done
    if (!s_cli->connected() && !s_cli->available()) return false;
    const int n = s_cli->read(buf, sizeof buf);
    if (n <= 0) { delay(5); lastYield = millis(); return true; }
    raw.concat((const char *)buf, (unsigned int)n);
    if (millis() - lastYield >= 50) { delay(1); lastYield = millis(); }
    return true;
  };

  int hdrEnd = -1;
  while (millis() < deadline) {
    hdrEnd = raw.indexOf("\r\n\r\n");
    if (hdrEnd >= 0) break;
    if (!pump()) break;
  }
  if (hdrEnd < 0) return false;

  String head = raw.substring(0, hdrEnd);
  head.toLowerCase();
  raw.remove(0, hdrEnd + 4);                      // in place: raw is now the body so far

  long len = -1;
  const int cl = head.indexOf("content-length:");
  if (cl >= 0) len = strtol(head.c_str() + cl + 15, nullptr, 10);

  if (len >= 0) {
    while ((long)raw.length() < len && millis() < deadline) {
      if (!pump()) break;
    }
    if ((long)raw.length() > len) raw.remove(len);
    keepAlive = ((long)raw.length() == len) && head.indexOf("connection: close") < 0;
  } else {
    while (millis() < deadline) {                 // no length: read until the server closes
      if (!pump()) break;
    }
  }
  heapwatch::note("amazon.body");   // raw body + TLS buffers both held — the heaviest point
  out = raw;
  return true;
}

// One SOAP round trip. `header` is the full <s:Header> contents. Returns the body, or "" on
// transport failure. HTTP status is not distinguished: a SOAP fault arrives as a 500 with a body we
// still need to read, so the caller inspects the body either way.
static String post(const String &action, const String &header, const String &body) {
  const String env = String("<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                            "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
                            "<s:Header>") + header + "</s:Header><s:Body>" + body +
                     "</s:Body></s:Envelope>";
  const String req = String("POST / HTTP/1.1\r\nHost: ") + kHost +
                     "\r\nContent-Type: text/xml; charset=\"utf-8\""
                     "\r\nSOAPAction: \"" + kNs + "#" + action + "\"" +
                     "\r\nUser-Agent: Linux UPnP/1.0 Sonos/84.1-59230"
                     "\r\nConnection: keep-alive\r\nContent-Length: " + String(env.length()) +
                     "\r\n\r\n";

  // Two attempts, but only when the first used a RECYCLED socket: a server that closed an idle
  // keep-alive connection looks identical to a failure until we try to write to it.
  for (int attempt = 0; attempt < 2; ++attempt) {
    const bool reused = (s_cli != nullptr);
    if (!ensureSession()) { LOG.println("[amazon] connect failed"); return ""; }

    s_cli->print(req);
    s_cli->print(env);

    String out;
    bool keepAlive = false;
    if (readResponse(out, keepAlive)) {
      s_lastUseMs = millis();
      if (!keepAlive) dropSession();
      return out;
    }
    dropSession();
    if (!reused) break;        // a brand-new connection failing is a real failure
  }
  return "";
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

// The ceremony is unauthenticated by definition — this is how we GET credentials. Both legs send
// the same empty-token header; an omitted <s:Header> is a 500 (WCF parse fault), so it is required
// even though it carries nothing.
static String anonCreds() {
  return String("<credentials xmlns=\"") + kNs +
         "\"><deviceProvider>Sonos</deviceProvider></credentials>";
}

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
  String r = post("getAppLink", anonCreds(), body);
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
  String r = post("getDeviceAuthToken", anonCreds(), body);
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
