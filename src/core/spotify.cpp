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
    else                                            it.kind = Item::Kind::Container;
    if (it.title.length() && it.id.length()) out.push_back(it);
  }
}

bool browse(const String &id, std::vector<Item> &out, int index, int count) {
  if (!linked()) return false;
  const String body = String("<getMetadata xmlns=\"") + kNs + "\"><id>" + escapeXml(id) +
                      "</id><index>" + String(index) + "</index><count>" + String(count) +
                      "</count></getMetadata>";
  eachItem(request("getMetadata", body), out, count);
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

// --- playback ------------------------------------------------------------------------------------

String playUri(const Item &it) {
  // Only the track form has a verified shape — it is the one this household's existing favourite
  // (FV:2/64) uses. Containers need an x-rincon-cpcontainer 8-hex prefix that cannot be derived
  // from anything readable; see spotify.h.
  if (it.kind != Item::Kind::Track) return "";
  return "x-sonos-spotify:" + urlEncode(it.id) + "?sid=" + String(kSid) +
         "&flags=8224&sn=" + String(settingsSpotifySerial());
}

String playMeta(const Item &it) {
  if (it.kind != Item::Kind::Track) return "";
  return String("<DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
                "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" "
                "xmlns:r=\"urn:schemas-rinconnetworks-com:metadata-1-0/\" "
                "xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">"
                "<item id=\"00032020") + urlEncode(it.id) +
         "\" parentID=\"-1\" restricted=\"true\">"
         "<dc:title>" + escapeXml(it.title) + "</dc:title>"
         "<upnp:class>object.item.audioItem.musicTrack</upnp:class>"
         "<desc id=\"cdudn\" nameSpace=\"urn:schemas-rinconnetworks-com:metadata-1-0/\">" +
         kDesc + "</desc></item></DIDL-Lite>";
}

}  // namespace spotify
