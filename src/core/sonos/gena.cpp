// See gena.h for what this replaces and why, and plans/09-gena-eventing.md for the measurements
// the design is built on.
#include "gena.h"

#ifdef GENA_EVENTS

#include <WiFi.h>

#include "didl.h"                 // parseNowPlaying() + xmlUnescape() — the inner escape layer
#include "../player_state.h"
#include "../net/wifi.h"
#include "../net/logmirror.h"     // LOG — reaches the TCP mirror on the jukebox, plain Serial
                                  // elsewhere. A wall-mounted unit has no cable, and these
                                  // diagnostics are exactly what you need to read from it.

namespace sonos {
namespace {

// --- tunables ---------------------------------------------------------------------------------

// Bound on one NOTIFY body. Measured worst case on the real system is 6,447 B for AVTransport
// (a track with long HLS URIs and full DIDL); 16 KB is generous headroom without being reckless
// on a board where internal SRAM is the binding constraint. An oversized body is REFUSED LOUDLY,
// never truncated — silently truncating a payload is exactly the album-art bug this project
// already paid for once (see JPEG_MAX in album_art.cpp).
constexpr size_t kMaxBody = 16 * 1024;

// Sonos grants Second-3600 (measured). Renew at half, so one lost renewal is not fatal.
constexpr uint32_t kRenewFraction = 2;
constexpr uint32_t kDefaultTimeoutS = 3600;

// Don't hammer a speaker that is refusing us.
constexpr uint32_t kRetryMs = 30000;

constexpr uint32_t kSockTimeoutMs = 4000;

struct Sub {
  const char *path;
  const char *label;
  String      sid;
  uint32_t    renewAtMs = 0;
  bool        live = false;
};

Sub s_subs[] = {
    {"/MediaRenderer/AVTransport/Event",      "avt", "", 0, false},
    {"/MediaRenderer/RenderingControl/Event", "rc",  "", 0, false},
};
constexpr size_t kNSubs = sizeof(s_subs) / sizeof(s_subs[0]);

WiFiServer *s_srv = nullptr;
uint16_t    s_port = 3401;
String      s_coordIp;
String      s_wantIp;          // set by genaSetCoordinator; picked up by genaTick on netTask
uint32_t    s_nextTryMs = 0;
volatile uint32_t s_events = 0, s_renewals = 0, s_resubs = 0, s_failures = 0;
volatile uint32_t s_lastEventMs = 0;
volatile bool     s_haveEvent = false;

// --- tiny helpers -----------------------------------------------------------------------------

// Value of a `<Tag val="..."/>` attribute in the already-once-unescaped Event XML. Returns the
// raw (still-escaped) attribute text — callers that need real content unescape it again.
String tagVal(const String &xml, const char *tag) {
  String needle = String("<") + tag + " ";
  int a = xml.indexOf(needle);
  if (a < 0) return "";
  int v = xml.indexOf("val=\"", a);
  if (v < 0) return "";
  v += 5;
  int e = xml.indexOf('"', v);
  if (e < 0) return "";
  return xml.substring(v, e);
}

// RenderingControl reports per-channel: <Volume channel="Master" val="34"/>. Anything that ignores
// the channel will happily read the LF or RF sub-channel and show the wrong number.
String channelVal(const String &xml, const char *tag, const char *channel) {
  String needle = String("<") + tag + " channel=\"" + channel + "\"";
  int a = xml.indexOf(needle);
  if (a < 0) return "";
  int v = xml.indexOf("val=\"", a);
  if (v < 0) return "";
  v += 5;
  int e = xml.indexOf('"', v);
  if (e < 0) return "";
  return xml.substring(v, e);
}

// "0:02:25" -> 145. Sonos also sends "NOT_IMPLEMENTED" and "0:00:00" for live streams.
uint32_t hmsToSec(const String &s) {
  int c1 = s.indexOf(':');
  if (c1 < 0) return 0;
  int c2 = s.indexOf(':', c1 + 1);
  if (c2 < 0) return 0;
  return (uint32_t)s.substring(0, c1).toInt() * 3600u +
         (uint32_t)s.substring(c1 + 1, c2).toInt() * 60u +
         (uint32_t)s.substring(c2 + 1).toInt();
}

TransportState parseTransport(const String &v) {
  if (v == "PLAYING")        return TransportState::Playing;
  if (v == "PAUSED_PLAYBACK") return TransportState::Paused;
  if (v == "STOPPED")        return TransportState::Stopped;
  if (v == "TRANSITIONING")  return TransportState::Transitioning;
  return TransportState::Unknown;
}

// --- applying an event ------------------------------------------------------------------------

void applyEvent(const String &body) {
  // Layer 1: the propertyset wrapper is escaped once. After this we hold the Event XML, whose
  // attribute values are STILL escaped (the DIDL blob doubly so) — see CLAUDE.md on double
  // escaping, and gena.h.
  const String ev = xmlUnescape(body);

  PlayerState np;
  bool gotTrack = false, gotTransport = false, gotVol = false, gotDur = false;
  TransportState st = TransportState::Unknown;
  uint32_t durSec = 0;
  uint8_t vol = 0;
  bool muted = false;

  const String ts = tagVal(ev, "TransportState");
  if (ts.length()) { st = parseTransport(ts); gotTransport = true; }

  const String dur = tagVal(ev, "CurrentTrackDuration");
  if (dur.length()) { durSec = hmsToSec(dur); gotDur = true; }

  // Still-escaped DIDL — parseNowPlaying() does the second unescape itself, exactly as it does for
  // the GetPositionInfo path. Reusing it keeps the two sources of now-playing data identical.
  const String meta = tagVal(ev, "CurrentTrackMetaData");
  if (meta.length() > 20) { parseNowPlaying(meta, np); gotTrack = true; }

  const String v = channelVal(ev, "Volume", "Master");
  if (v.length()) { vol = (uint8_t)constrain(v.toInt(), 0, 100); gotVol = true; }
  const String m = channelVal(ev, "Mute", "Master");
  const bool gotMute = m.length() > 0;
  if (gotMute) muted = (m.toInt() != 0);

  if (!(gotTransport || gotTrack || gotVol || gotMute || gotDur)) return;

  if (!stateLock()) return;
  if (gotTransport) g_player.transport = st;
  if (gotDur)       g_player.durationSec = durSec;
  if (gotVol)       g_player.volume = vol;
  if (gotMute)      g_player.muted = muted;
  if (gotTrack) {
    // Only overwrite when the event actually carried a track. An empty DIDL (between tracks, or a
    // volume-only event) must not blank Now Playing.
    if (np.title.length() || np.artist.length()) {
      g_player.title  = np.title;
      g_player.artist = np.artist;
      g_player.album  = np.album;
      g_player.artUri = np.artUri;
      // Position is NOT evented (measured: 83 s of playback, no event). A new track means the
      // position restarts; the UI's own interpolation carries it from here, and the backstop poll
      // reconciles drift.
      g_player.positionSec = 0;
    }
  }
  g_player.dirty = true;
  stateUnlock();

  s_lastEventMs = millis();
  s_haveEvent = true;
  s_events++;
}

// --- callback listener ------------------------------------------------------------------------

// Read one line (CRLF-terminated) with a deadline. Returns false on timeout/disconnect.
bool readLine(WiFiClient &c, String &out, uint32_t deadline) {
  out = "";
  while (millis() < deadline) {
    if (!c.connected() && !c.available()) return false;
    if (!c.available()) { delay(1); continue; }
    char ch = (char)c.read();
    if (ch == '\n') { if (out.endsWith("\r")) out.remove(out.length() - 1); return true; }
    out += ch;
    if (out.length() > 512) return true;   // absurd header line; take what we have
  }
  return false;
}

void serveOne(WiFiClient &c) {
  const uint32_t deadline = millis() + kSockTimeoutMs;
  String line;
  if (!readLine(c, line, deadline)) { c.stop(); return; }

  // We only ever answer NOTIFY. Anything else gets a 405 rather than being parsed.
  const bool isNotify = line.startsWith("NOTIFY");

  long clen = -1;
  bool chunked = false;
  while (readLine(c, line, deadline)) {
    if (line.length() == 0) break;               // end of headers
    String lower = line; lower.toLowerCase();
    if (lower.startsWith("content-length:")) clen = line.substring(15).toInt();
    else if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") >= 0) chunked = true;
  }

  if (!isNotify) {
    c.print(F("HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
    c.stop();
    return;
  }

  // Refuse loudly rather than truncate. A half-parsed event would show wrong track info, which is
  // worse than a missed one — the backstop poll fixes a miss.
  if (chunked || clen < 0 || (size_t)clen > kMaxBody) {
    LOG.printf("[gena  ] refusing body (len=%ld chunked=%d max=%u)\n",
                  clen, (int)chunked, (unsigned)kMaxBody);
    c.print(F("HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
    c.stop();
    return;
  }

  String body;
  body.reserve((size_t)clen + 1);
  while ((long)body.length() < clen && millis() < deadline) {
    if (!c.available()) {
      if (!c.connected()) break;
      delay(1);
      continue;
    }
    body += (char)c.read();
  }

  // Answer BEFORE parsing. Sonos drops a subscription whose callback is slow, and the unescape +
  // DIDL parse below is the expensive part.
  c.print(F("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
  c.flush();
  c.stop();

  if ((long)body.length() == clen) applyEvent(body);
  else LOG.printf("[gena  ] short body %u/%ld\n", (unsigned)body.length(), clen);
}

void listenerTask(void *) {
  // boardInit()/appBoot() ordering means Wi-Fi is usually not up yet — wait for it here rather
  // than making the caller sequence it, the same way the sleep-machine's media httpd does.
  while (!wifiIsConnected()) vTaskDelay(pdMS_TO_TICKS(500));

  s_srv = new WiFiServer(s_port);
  s_srv->begin();
  s_srv->setNoDelay(true);
  LOG.printf("[gena  ] callback listening on %s:%u\n",
                WiFi.localIP().toString().c_str(), (unsigned)s_port);

  for (;;) {
    WiFiClient c = s_srv->available();
    if (c) serveOne(c);
    else   vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// --- subscription management (netTask only) -----------------------------------------------------

// One raw SUBSCRIBE/UNSUBSCRIBE. HTTPClient is avoided deliberately: these are non-standard verbs
// with non-standard headers (CALLBACK/NT/SID/TIMEOUT) and we must read SID/TIMEOUT back off the
// RESPONSE, which HTTPClient does not expose cleanly.
bool genaVerb(const char *verb, const String &ip, const char *path, const String &extraHeaders,
              String *sidOut, uint32_t *timeoutOut) {
  WiFiClient c;
  c.setTimeout(kSockTimeoutMs / 1000);
  if (!c.connect(ip.c_str(), 1400)) return false;

  String req = String(verb) + " " + path + " HTTP/1.1\r\nHOST: " + ip + ":1400\r\n" +
               extraHeaders + "Connection: close\r\n\r\n";
  c.print(req);

  const uint32_t deadline = millis() + kSockTimeoutMs;
  String line;
  if (!readLine(c, line, deadline)) { c.stop(); return false; }
  const bool ok = line.indexOf(" 200") > 0;
  const bool precondFailed = line.indexOf(" 412") > 0;

  while (readLine(c, line, deadline)) {
    if (line.length() == 0) break;
    String lower = line; lower.toLowerCase();
    if (sidOut && lower.startsWith("sid:")) {
      *sidOut = line.substring(4);
      sidOut->trim();
    } else if (timeoutOut && lower.startsWith("timeout:")) {
      const int d = line.indexOf("Second-");
      if (d >= 0) *timeoutOut = (uint32_t)line.substring(d + 7).toInt();
    }
  }
  c.stop();
  if (precondFailed) return false;
  return ok;
}

void dropSub(Sub &s) {
  if (s.live && s.sid.length() && s_coordIp.length()) {
    genaVerb("UNSUBSCRIBE", s_coordIp, s.path, "SID: " + s.sid + "\r\n", nullptr, nullptr);
  }
  s.sid = "";
  s.live = false;
  s.renewAtMs = 0;
}

bool subscribe(Sub &s) {
  const String cb = "<http://" + WiFi.localIP().toString() + ":" + String(s_port) + "/>";
  String sid;
  uint32_t tmo = kDefaultTimeoutS;
  const String hdrs = "CALLBACK: " + cb + "\r\nNT: upnp:event\r\nTIMEOUT: Second-" +
                      String(kDefaultTimeoutS) + "\r\n";
  if (!genaVerb("SUBSCRIBE", s_coordIp, s.path, hdrs, &sid, &tmo) || sid.length() == 0) {
    s_failures++;
    return false;
  }
  s.sid = sid;
  s.live = true;
  if (tmo == 0) tmo = kDefaultTimeoutS;
  s.renewAtMs = millis() + (tmo * 1000u) / kRenewFraction;
  LOG.printf("[gena  ] %s subscribed sid=%s timeout=%us\n", s.label, s.sid.c_str(),
                (unsigned)tmo);
  return true;
}

bool renew(Sub &s) {
  uint32_t tmo = kDefaultTimeoutS;
  // Renewal MUST resend the SID and MUST NOT send CALLBACK/NT. A 412 means the subscription is
  // gone at the speaker's end (it rebooted, or we missed the window) — retrying the renew forever
  // is the documented Home Assistant failure; fall back to a fresh SUBSCRIBE instead.
  if (!genaVerb("SUBSCRIBE", s_coordIp, s.path,
                "SID: " + s.sid + "\r\nTIMEOUT: Second-" + String(kDefaultTimeoutS) + "\r\n",
                nullptr, &tmo)) {
    LOG.printf("[gena  ] %s renew failed -> resubscribing\n", s.label);
    s.sid = ""; s.live = false;
    s_resubs++;
    return subscribe(s);
  }
  if (tmo == 0) tmo = kDefaultTimeoutS;
  s.renewAtMs = millis() + (tmo * 1000u) / kRenewFraction;
  s_renewals++;
  return true;
}

}  // namespace

// --- api ----------------------------------------------------------------------------------------

void genaBegin(uint16_t port) {
  if (s_srv) return;
  s_port = port;
  // Core 1: core 0 belongs to the network tasks. Small stack — this task parses, it does not
  // recurse. Priority below netTask so a burst of events cannot starve command processing.
  xTaskCreatePinnedToCore(listenerTask, "gena", 4096, nullptr, 1, nullptr, 1);
}

void genaSetCoordinator(const String &coordIp) { s_wantIp = coordIp; }

void genaTick() {
  if (!wifiIsConnected() || !s_srv) return;

  // Coordinator moved (or first run): tear the old subscriptions down and start again. Grouping
  // changes move it, which is why processPending bumps g_zonesGen at those points.
  if (s_wantIp.length() && s_wantIp != s_coordIp) {
    for (size_t i = 0; i < kNSubs; ++i) dropSub(s_subs[i]);
    s_coordIp = s_wantIp;
    s_nextTryMs = 0;
    LOG.printf("[gena  ] coordinator -> %s\n", s_coordIp.c_str());
  }
  if (s_coordIp.length() == 0) return;

  const uint32_t now = millis();
  for (size_t i = 0; i < kNSubs; ++i) {
    Sub &s = s_subs[i];
    if (!s.live) {
      if ((int32_t)(now - s_nextTryMs) < 0) continue;
      if (!subscribe(s)) s_nextTryMs = now + kRetryMs;   // back off ALL of them together
      return;                                            // one blocking call per tick, max
    }
    if ((int32_t)(now - s.renewAtMs) >= 0) {
      renew(s);
      return;
    }
  }
}

void genaDiag(GenaDiag &out) {
  out.subscribed = true;
  for (size_t i = 0; i < kNSubs; ++i) out.subscribed = out.subscribed && s_subs[i].live;
  out.events = s_events;
  out.lastEventAgeMs = s_haveEvent ? (millis() - s_lastEventMs) : UINT32_MAX;
  out.renewals = s_renewals;
  out.resubscribes = s_resubs;
  out.failures = s_failures;
  out.port = s_port;
}

}  // namespace sonos

#endif  // GENA_EVENTS
