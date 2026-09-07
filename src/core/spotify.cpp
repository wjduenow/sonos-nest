// See spotify.h. SMAPI client for Spotify (service id 12, type 3079 = 12*256+7).
//
// This is deliberately shaped like amazon.cpp — same ceremony, same token-refresh retry, same
// blocking-HTTPS-off-the-UI-task rule — because they are the same protocol against a different
// endpoint. The transport, the XML helpers and the credentials headers are shared in core/smapi.
//
// Three things here are specific to Spotify and were established by running it:
//
//  1. THE LINK CODE DIES IN ~5 MINUTES, silently, in the browser first. See spotify.h.
//  2. IDS ARE NATIVE AND TRANSPARENT: search returns "spotify:track:6vLaKD0HUJ5UtIADG61Fa9", which
//     is the id Sonos itself wraps. That is what makes playback constructible at all — contrast
//     YouTube Music, whose Sonos id is an opaque account-scoped token (plans/08).
//  3. THERE IS NO STATION OR STREAM itemType. Spotify's tree is playlists all the way down, so
//     anything this module offers a Radio page is a playlist and should be labelled as one.
#include "spotify.h"

#include "smapi.h"
#include "settings.h"
#include "sonos/soap_client.h"
#include "sonos/ssdp.h"
#include "net/wifi.h"        // wifiHostname — our linkDeviceId, as in amazon.cpp
#include "net/logmirror.h"   // LOG — tees to the TCP mirror where enabled

namespace spotify {

using smapi::escapeXml;
using smapi::tagValue;
using smapi::unescapeXml;
using smapi::urlEncode;

static const char *kHost = "spotify-v5.ws.sonos.com";
// ⚠️ NOT "/". Spotify serves SMAPI at /smapi and answers "/" with a 404 that reads exactly like an
// outage. This is why smapi::Client takes a path.
static const char *kPath = "/smapi";
static const char *kNs   = "http://www.sonos.com/Services/1.1";
static const int   kSid  = 12;
static const int   kType = 3079;                          // kSid * 256 + 7
static const char *kDesc = "SA_RINCON3079_X_#Svc3079-0-Token";

static smapi::Client s_client(kHost, kPath, "spotify");

void endSession() { s_client.endSession(); }

// --- requests ------------------------------------------------------------------------------------

static String credsHeader() {
  return smapi::loginCreds(settingsSpotifyToken(), settingsSpotifyKey(), settingsHouseholdId());
}

// An authenticated request with the one recovery that matters: an expired token comes back as a
// fault CARRYING its own replacement, so extract, persist and retry exactly once. Amazon does this
// and does NOT implement refreshAuthToken as an operation; whether Spotify behaves identically is
// unverified, so the log line below is the thing to look for if a link ever goes stale.
static String request(const String &action, const String &body) {
  String r = s_client.post(action, credsHeader(), body);
  if (r.indexOf("TokenRefreshRequired") < 0) return r;

  const String tok = unescapeXml(tagValue(r, "authToken"));
  const String key = unescapeXml(tagValue(r, "privateKey"));
  if (tok.isEmpty()) {
    LOG.println("[spotify] token expired and the fault carried no replacement — re-link needed");
    return r;
  }
  settingsSetSpotifyAuth(tok, key);
  LOG.printf("[spotify] token refreshed in-band (%u/%u chars), retrying\n",
             (unsigned)tok.length(), (unsigned)key.length());
  return s_client.post(action, credsHeader(), body);
}

// --- household values ----------------------------------------------------------------------------

void adopt() {
  if (settingsHouseholdId().length() && settingsSpotifySerial()) return;
  std::vector<sonos::Zone> zs;
  sonos::zonesSnapshot(zs);
  if (zs.empty()) return;
  const String ip = zs[0].ip;

  if (settingsHouseholdId().isEmpty()) {
    String r;
    if (sonos::soapAction(ip, "/DeviceProperties/Control",
                          "urn:schemas-upnp-org:service:DeviceProperties:1",
                          "GetHouseholdID", "", r)) {
      const String hh = tagValue(r, "CurrentHouseholdID");
      if (hh.length()) settingsSetHouseholdId(hh);
    }
  }

  // The sn= parameter, exactly as amazon.cpp reads its own: our token is NOT the account that will
  // play, so the serial has to come from the household's existing Spotify favourites. A household
  // with none leaves this 0, which is untested for playback.
  if (!settingsSpotifySerial()) {
    String r;
    if (sonos::soapAction(ip, "/MediaServer/ContentDirectory/Control",
                          "urn:schemas-upnp-org:service:ContentDirectory:1", "Browse",
                          "<ObjectID>FV:2</ObjectID><BrowseFlag>BrowseDirectChildren</BrowseFlag>"
                          "<Filter>*</Filter><StartingIndex>0</StartingIndex>"
                          "<RequestedCount>100</RequestedCount><SortCriteria></SortCriteria>", r)) {
      const int at = r.indexOf("sid=12");
      const int sn = (at < 0) ? -1 : r.indexOf("sn=", at);
      if (sn > 0) {
        const uint8_t v = (uint8_t)strtoul(r.substring(sn + 3, sn + 6).c_str(), nullptr, 10);
        if (v) { settingsSetSpotifySerial(v); LOG.printf("[spotify] account serial sn=%u\n", v); }
      }
    }
  }
}

// --- linking -------------------------------------------------------------------------------------

bool linked() { return settingsSpotifyToken().length() > 0; }

static String s_linkCode;
static String linkDeviceId() {
  const String h = wifiHostname();
  return h.length() ? h : String("sonos-jukebox");
}

void unlink() { settingsSetSpotifyAuth("", ""); s_linkCode = ""; }

static bool linkBegin(String &regUrlOut) {
  const String body = String("<getAppLink xmlns=\"") + kNs + "\"><householdId>" +
                      escapeXml(settingsHouseholdId()) + "</householdId></getAppLink>";
  const String r = s_client.post("getAppLink", smapi::anonCreds(), body);
  regUrlOut  = unescapeXml(tagValue(r, "regUrl"));
  s_linkCode = unescapeXml(tagValue(r, "linkCode"));
  if (regUrlOut.isEmpty() || s_linkCode.isEmpty()) {
    const String fault = unescapeXml(tagValue(r, "faultstring"));
    LOG.printf("[spotify] link begin failed (%s)\n",
               fault.length() ? fault.c_str() : "no regUrl/linkCode in the response");
    return false;
  }
  return true;
}

static bool linkPoll() {
  if (s_linkCode.isEmpty()) return false;
  const String body = String("<getDeviceAuthToken xmlns=\"") + kNs + "\"><householdId>" +
                      escapeXml(settingsHouseholdId()) + "</householdId><linkCode>" +
                      escapeXml(s_linkCode) + "</linkCode><linkDeviceId>" +
                      escapeXml(linkDeviceId()) + "</linkDeviceId></getDeviceAuthToken>";
  const String r = s_client.post("getDeviceAuthToken", smapi::anonCreds(), body);
  const String tok = unescapeXml(tagValue(r, "authToken"));
  if (tok.isEmpty()) return false;      // NOT_LINKED_RETRY while the owner is still approving
  settingsSetSpotifyAuth(tok, unescapeXml(tagValue(r, "privateKey")));
  s_linkCode = "";
  LOG.println("[spotify] linked");
  return true;
}

// The ceremony on its own task: both legs are blocking HTTPS and the UI must never make them.
//
// The window is 300 s because that is Spotify's measured code lifetime, NOT the 420 s amazon.cpp
// uses — theirs is Sonos's own app polling interval and Amazon's code outlives it comfortably.
// Expiring the state ourselves at the same moment the code dies is what lets the UI re-mint instead
// of showing a QR that stopped working two minutes ago.
static volatile LinkState s_state = LinkState::Idle;
static String            s_regUrl;
static volatile uint16_t s_left = 0;
static TaskHandle_t      s_linkTask = nullptr;

LinkState linkState()       { return s_state; }
String    linkUrl()         { return s_regUrl; }
uint16_t  linkSecondsLeft() { return s_left; }
void      linkCancel()      { if (s_state == LinkState::Waiting || s_state == LinkState::Starting)
                                s_state = LinkState::Idle; }

static void linkTask(void *) {
  for (;;) {
    if (s_state != LinkState::Starting) { vTaskDelay(pdMS_TO_TICKS(250)); continue; }

    adopt();                          // the household id is required to ask for a code
    String url;
    if (!linkBegin(url)) { s_state = LinkState::Failed; continue; }
    s_regUrl = url;
    s_left   = 300;                   // five minutes — see the note above
    s_state  = LinkState::Waiting;

    while (s_state == LinkState::Waiting && s_left > 0) {
      vTaskDelay(pdMS_TO_TICKS(3000));
      s_left = (s_left > 3) ? (uint16_t)(s_left - 3) : 0;
      if (s_state != LinkState::Waiting) break;     // cancelled from the UI
      if (linkPoll()) { s_state = LinkState::Linked; break; }
    }
    if (s_state == LinkState::Waiting) s_state = LinkState::Failed;   // window closed unused
    s_client.endSession();
  }
}

void linkStart() {
  if (s_state == LinkState::Starting || s_state == LinkState::Waiting) return;
  s_regUrl = ""; s_left = 0;
  s_state = LinkState::Starting;
  // Core 0 with the network. Created lazily so a device that never links never spawns it.
  if (!s_linkTask) xTaskCreatePinnedToCore(linkTask, "splink", 6144, nullptr, 1, &s_linkTask, 0);
}

// --- browse + search -----------------------------------------------------------------------------

// Walks <mediaCollection> AND <mediaMetadata> blocks in document order. Both matter here and the
// difference is the item's kind, not its importance: a search for tracks returns mediaMetadata, a
// search for albums returns mediaCollection, and a browse of "root" returns only collections.
static void eachItem(const String &xml, std::vector<Item> &out, int max) {
  int p = 0;
  while ((int)out.size() < max) {
    const int c = xml.indexOf("mediaCollection>", p);
    const int m = xml.indexOf("mediaMetadata>", p);
    if (c < 0 && m < 0) break;
    const bool coll = (c >= 0) && (m < 0 || c < m);
    const int s = coll ? c : m;
    const char *close = coll ? "mediaCollection>" : "mediaMetadata>";
    const int open = xml.lastIndexOf('<', s);
    const int e = xml.indexOf("</", s);
    const int end = (e < 0) ? -1 : xml.indexOf(close, e);
    if (open < 0 || end < 0) break;
    const String blk = xml.substring(open, end + (int)strlen(close));
    p = end + (int)strlen(close);

    Item it;
    it.title    = unescapeXml(tagValue(blk, "title"));
    it.id       = unescapeXml(tagValue(blk, "id"));
    it.artUrl   = unescapeXml(tagValue(blk, "albumArtURI"));
    it.subtitle = unescapeXml(tagValue(blk, "artist"));
    if (it.subtitle.isEmpty()) it.subtitle = unescapeXml(tagValue(blk, "owner"));
    // itemType is the service's word for it; the id prefix agrees and is the simpler check.
    if      (it.id.startsWith("spotify:track:"))    it.kind = Item::Kind::Track;
    else if (it.id.startsWith("spotify:artist:"))   it.kind = Item::Kind::Artist;
    else if (it.id.startsWith("spotify:album:"))    it.kind = Item::Kind::Album;
    else if (it.id.startsWith("spotify:playlist:")) it.kind = Item::Kind::Playlist;
    else if (it.id.startsWith("spotify:artistRadio:")) it.kind = Item::Kind::Station;
    else                                            it.kind = Item::Kind::Container;
    if (it.title.length() && it.id.length()) out.push_back(it);
  }
}

bool browse(const String &id, std::vector<Item> &out, int index, int count) {
  if (!linked()) return false;
  const String body = String("<getMetadata xmlns=\"") + kNs + "\"><id>" + escapeXml(id) +
                      "</id><index>" + String(index) + "</index><count>" + String(count) +
                      "</count></getMetadata>";
  const String r = request("getMetadata", body);
  eachItem(r, out, count);
  // An empty result is reported to the UI as a flat failure, which on a device with no serial port
  // is indistinguishable from a dead network. Say which it was: a transport failure returns "", a
  // rejected request returns a fault, and a genuinely empty container returns neither.
  if (out.empty()) {
    const String fault = unescapeXml(tagValue(r, "faultstring"));
    LOG.printf("[spotify] browse %s -> %u B, no items%s%s\n", smapi::cstr(id), (unsigned)r.length(),
               fault.length() ? ", fault: " : "", fault.length() ? smapi::cstr(fault) : "");
  }
  return !out.empty();
}

// The service's own category id. Spotify's come from its presentation map's <PresentationMap
// type="Search"> block, and they are NOT universal — YouTube Music maps the same five ids to
// ARTISTS/ALBUMS/SONGS/PLAYLISTS/ALL. A second service belongs in this table, read from its
// presentation map once at link time, rather than in new code.
static const char *categoryId(Category c) {
  switch (c) {
    case Category::Tracks:    return "track";
    case Category::Artists:   return "artist";
    case Category::Albums:    return "album";
    case Category::Playlists: return "playlist";
    case Category::All:
    default:                  return "all";
  }
}

bool search(const String &term, Category cat, std::vector<Item> &out, int count) {
  if (!linked() || term.isEmpty()) return false;
  const String body = String("<search xmlns=\"") + kNs + "\"><id>" + categoryId(cat) +
                      "</id><term>" + escapeXml(term) + "</term><index>0</index><count>" +
                      String(count) + "</count></search>";
  eachItem(request("search", body), out, count);
  return !out.empty();
}

// --- search, asynchronously ----------------------------------------------------------------------
// One worker, one pending query. A second searchStart() while one is running REPLACES the pending
// term rather than queueing: the user typing another letter means the in-flight query is already
// stale, and a queue would make the UI walk through every intermediate result before showing the
// one that was asked for last.
// 8192, MATCHING EVERY OTHER TASK HERE THAT DOES TLS AND XML: radiocache (the Amazon crawl, the
// same work over larger responses), favcache, netTask, artTask, uiTask. This was 6144, copied from
// the link task — and the link task is not a precedent, because it only ever handles a link code
// and a token, under a kilobyte each.
//
// The difference is not theoretical. At 6144 a search survived (3-6 KB responses) and the first
// real browse did not: `Radio -> Spotify -> Genres and Moods` is 15.7 KB and the device rebooted
// with task `spsearch`, mcause 5, mtval 0 and a PC that does not resolve in the ELF — a corrupted
// return address, i.e. the stack, not a null dereference in the parser. If a task here needs to
// hold a SOAP response, it needs 8192.
static const uint32_t kWorkerStack = 8192;

static SemaphoreHandle_t  s_searchMx = nullptr;
static TaskHandle_t       s_searchTask = nullptr;
static volatile SearchState s_searchState = SearchState::Idle;
static volatile uint32_t  s_searchGen = 0;
static String             s_pendingTerm;
static Category           s_pendingCat = Category::All;
static volatile bool      s_pendingHas = false;
static std::vector<Item>  s_results;
static String             s_pendingBrowse;
static volatile bool      s_browsePendingHas = false;
static volatile SearchState s_browseState = SearchState::Idle;
static volatile uint32_t  s_browseGen = 0;
static std::vector<Item>  s_browseResults;

SearchState searchState() { return s_searchState; }
uint32_t    searchGen()   { return s_searchGen; }

bool searchResults(std::vector<Item> &out) {
  if (!s_searchMx) return false;
  xSemaphoreTake(s_searchMx, portMAX_DELAY);
  out = s_results;
  xSemaphoreGive(s_searchMx);
  return true;
}

SearchState browseState() { return s_browseState; }
uint32_t    browseGen()   { return s_browseGen; }

bool browseResults(std::vector<Item> &out) {
  if (!s_searchMx) return false;
  xSemaphoreTake(s_searchMx, portMAX_DELAY);
  out = s_browseResults;
  xSemaphoreGive(s_searchMx);
  return true;
}

static void searchTask(void *) {
  for (;;) {
    if (s_browsePendingHas) {
      xSemaphoreTake(s_searchMx, portMAX_DELAY);
      const String id = s_pendingBrowse;
      s_browsePendingHas = false;
      xSemaphoreGive(s_searchMx);

      s_browseState = SearchState::Running;
      std::vector<Item> found;
      const bool ok = browse(id, found, 0, 60);
      xSemaphoreTake(s_searchMx, portMAX_DELAY);
      s_browseResults = found;
      xSemaphoreGive(s_searchMx);
      s_browseGen++;
      s_browseState = ok ? SearchState::Done : SearchState::Failed;
      endSession();
      // The margin, every time, because the way this task fails is a reboot with no log line and a
      // PC that does not resolve. Anything under ~1 KB here means the next larger response is a
      // crash rather than a slow list.
      LOG.printf("[spotify] browse %s: %d items, stack free %u B\n", smapi::cstr(id), (int)found.size(),
                 (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
      continue;
    }
    if (!s_pendingHas) { vTaskDelay(pdMS_TO_TICKS(60)); continue; }

    xSemaphoreTake(s_searchMx, portMAX_DELAY);
    const String term = s_pendingTerm;
    const Category cat = s_pendingCat;
    s_pendingHas = false;
    xSemaphoreGive(s_searchMx);

    s_searchState = SearchState::Running;
    std::vector<Item> found;
    const bool ok = search(term, cat, found, 8);

    // A newer query arrived while this one was in flight: drop this result on the floor rather than
    // showing it for the moment before the newer one lands.
    if (s_pendingHas) continue;

    xSemaphoreTake(s_searchMx, portMAX_DELAY);
    s_results = found;
    xSemaphoreGive(s_searchMx);
    s_searchGen++;
    s_searchState = ok ? SearchState::Done : SearchState::Failed;
    endSession();     // one user-initiated burst; do not hold the socket (smapi.h)
  }
}

void browseStart(const String &id) {
  if (!s_searchMx) s_searchMx = xSemaphoreCreateMutex();
  if (!s_searchMx) return;
  xSemaphoreTake(s_searchMx, portMAX_DELAY);
  s_pendingBrowse    = id;
  s_browsePendingHas = true;
  xSemaphoreGive(s_searchMx);
  s_browseState = SearchState::Running;
  // Logged on the WAY IN, not just on completion. Without this, "the request went out and came
  // back empty" and "the tap never reached a request at all" produce the same silence, and they
  // are opposite bugs — one is the service or the parser, the other is the UI.
  LOG.printf("[spotify] browse start %s\n", smapi::cstr(id));
  if (!s_searchTask) xTaskCreatePinnedToCore(searchTask, "spsearch", kWorkerStack, nullptr, 1, &s_searchTask, 0);
}

void searchStart(const String &term, Category cat) {
  if (!s_searchMx) s_searchMx = xSemaphoreCreateMutex();
  if (!s_searchMx) return;
  xSemaphoreTake(s_searchMx, portMAX_DELAY);
  s_pendingTerm = term;
  s_pendingCat  = cat;
  s_pendingHas  = true;
  xSemaphoreGive(s_searchMx);
  s_searchState = SearchState::Running;
  // Core 0 with the network. Lazily created: a device that never searches never spawns it.
  if (!s_searchTask) xTaskCreatePinnedToCore(searchTask, "spsearch", kWorkerStack, nullptr, 1, &s_searchTask, 0);
}

// --- playback ------------------------------------------------------------------------------------

String playUri(const Item &it) {
  // Tracks use the form this household's existing favourite (FV:2/64) proves. Stations
  // (itemType=program, i.e. artist radio) take the x-sonosapi-radio: form that Amazon's stations
  // use — same item type, same scheme, and amazon::playUri is the working reference for it.
  // UNVERIFIED for Spotify: nothing here has played one yet.
  //
  // Containers still return "": album/artist/playlist need an x-rincon-cpcontainer 8-hex prefix
  // that cannot be derived from anything readable. They do not need one — they BROWSE into tracks
  // (an album yields its tracks, an artist yields Queen Radio + Top Tracks + albums), which is a
  // drill-down rather than a construction and needs no guessing.
  if (it.kind == Item::Kind::Track)
    return "x-sonos-spotify:" + urlEncode(it.id) + "?sid=" + String(kSid) +
           "&flags=8224&sn=" + String(settingsSpotifySerial());
  if (it.kind == Item::Kind::Station)
    return "x-sonosapi-radio:" + urlEncode(it.id) + "?sid=" + String(kSid) +
           "&flags=8300&sn=" + String(settingsSpotifySerial());
  return "";
}

String playMeta(const Item &it) {
  if (it.kind != Item::Kind::Track && it.kind != Item::Kind::Station) return "";
  const bool station = (it.kind == Item::Kind::Station);
  return String("<DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
                "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" "
                "xmlns:r=\"urn:schemas-rinconnetworks-com:metadata-1-0/\" "
                "xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">"
                "<item id=\"00032020") + urlEncode(it.id) +
         "\" parentID=\"-1\" restricted=\"true\">"
         "<dc:title>" + escapeXml(it.title) + "</dc:title>"
         "<upnp:class>" + (station ? "object.item.audioItem.audioBroadcast"
                                   : "object.item.audioItem.musicTrack") + "</upnp:class>"
         "<desc id=\"cdudn\" nameSpace=\"urn:schemas-rinconnetworks-com:metadata-1-0/\">" +
         kDesc + "</desc></item></DIDL-Lite>";
}

}  // namespace spotify
