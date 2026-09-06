// See smapi.h. Transport and XML for the Sonos Music API, lifted out of amazon.cpp unchanged —
// every non-obvious line below was established by breaking something first, and the comments
// explaining which are kept with the code rather than left behind.
#include "smapi.h"

#include <WiFiClientSecure.h>

#include <utility>   // std::move

#include "heap_watch.h"       // heapwatch::note — attribute the internal-heap low-water
#include "net/logmirror.h"    // LOG — tees to the TCP mirror where enabled

namespace smapi {

const char *kNs = "http://www.sonos.com/Services/1.1";

// --- XML helpers ---------------------------------------------------------------------------------

String tagValue(const String &xml, const char *tag, int from) {
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
    if (sp >= 0) name = name.substring(0, sp);        // drop attributes
    if (name.endsWith("/")) { p = gt + 1; continue; }  // self-closing: no value
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

String unescapeXml(String s) {
  s.replace("&lt;", "<");    s.replace("&gt;", ">");
  s.replace("&quot;", "\""); s.replace("&apos;", "'");
  s.replace("&amp;", "&");   // last, or the others double-decode
  return s;
}

String escapeXml(const String &in) {
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

String urlEncode(const String &in) {
  static const char *hex = "0123456789abcdef";
  String o; o.reserve(in.length() * 2);
  for (size_t i = 0; i < in.length(); ++i) {
    const unsigned char c = (unsigned char)in[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { o += '%'; o += hex[c >> 4]; o += hex[c & 0xF]; }
  }
  return o;
}

// --- credentials ---------------------------------------------------------------------------------

String anonCreds() {
  return String("<credentials xmlns=\"") + kNs +
         "\"><deviceProvider>Sonos</deviceProvider></credentials>";
}

String loginCreds(const String &token, const String &key, const String &householdId) {
  return String("<credentials xmlns=\"") + kNs + "\"><deviceProvider>Sonos</deviceProvider>"
         "<loginToken><token>" + escapeXml(token) +
         "</token><key>" + escapeXml(key) +
         "</key><householdId>" + escapeXml(householdId) +
         "</householdId></loginToken></credentials>";
}

// --- Client --------------------------------------------------------------------------------------

Client::Client(const char *host, const char *path, const char *logTag)
    : host_(host), path_(path), tag_(logTag) {
  bodyTag_ = String(logTag) + ".body";
}

void Client::dropSession() {
  if (!cli_) return;
  cli_->stop();
  delete cli_;
  cli_ = nullptr;
}

void Client::endSession() { dropSession(); }

bool Client::ensureSession() {
  static const uint32_t kIdleDropMs = 30000;   // a session idle this long is presumed dead
  if (cli_) {
    if (cli_->connected() && (millis() - lastUseMs_) < kIdleDropMs) return true;
    dropSession();
  }
  cli_ = new WiFiClientSecure();
  if (!cli_) return false;
  cli_->setInsecure();        // same posture as core/net/updater.cpp — no cert store on device
  cli_->setTimeout(15000);
  if (!cli_->connect(host_, 443)) { dropSession(); return false; }
  return true;
}

// Reads one complete HTTP response. `keepAlive` reports whether the socket is still in a known
// state afterwards. False return = the response never arrived.
//
// Reuse forces us to know where a response ENDS, which read-to-EOF never had to. Content-Length is
// honoured and the socket kept; anything else (chunked, or no length at all) falls back to
// read-until-close and drops the session, so a server that will not do keep-alive is merely no
// worse than before. Deliberately no de-chunker: it needs a second copy of a ~19 KB body and the
// jukebox crawl runs with ~40 KB of internal heap free.
//
// ⚠️ NEVER use readStringUntil() or any Stream helper on a TLS socket here. Two compounding traps,
// which together rebooted the jukebox in a loop:
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
bool Client::readResponse(String &out, bool &keepAlive) {
  const uint32_t deadline = millis() + 20000;
  uint8_t buf[1024];
  uint32_t lastYield = millis();
  keepAlive = false;

  String raw;
  // 4 KB to start, then exactly Content-Length once the headers say so. This used to reserve 24 KB
  // unconditionally — 24 KB of contiguous internal heap for a 1.9 KB response, on a board whose
  // largest free block is ~36-43 KB.
  raw.reserve(4 * 1024);
  auto pump = [&]() -> bool {                     // one block, yielding; false = socket done
    if (!cli_->connected() && !cli_->available()) return false;
    const int n = cli_->read(buf, sizeof buf);
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

  if (len > (long)raw.length()) raw.reserve((unsigned)len + 64);

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
  heapwatch::note(bodyTag_.c_str());   // raw body + TLS buffers both held — the heaviest point
  // MOVE, never copy. `out = raw` allocated a second buffer the size of the body and held both at
  // once — 32-48 KB of internal heap for a 16-22 KB response, which is how a browse of a large
  // container ran the heap out and left an INVALIDATED String behind (buffer == nullptr).
  out = std::move(raw);
  return true;
}

String Client::post(const String &action, const String &header, const String &body) {
  const String env = String("<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                            "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
                            "<s:Header>") + header + "</s:Header><s:Body>" + body +
                     "</s:Body></s:Envelope>";
  const String req = String("POST ") + path_ + " HTTP/1.1\r\nHost: " + host_ +
                     "\r\nContent-Type: text/xml; charset=\"utf-8\""
                     "\r\nSOAPAction: \"" + kNs + "#" + action + "\"" +
                     "\r\nUser-Agent: Linux UPnP/1.0 Sonos/84.1-59230"
                     "\r\nConnection: keep-alive\r\nContent-Length: " + String(env.length()) +
                     "\r\n\r\n";

  // Two attempts, but only when the first used a RECYCLED socket: a server that closed an idle
  // keep-alive connection looks identical to a failure until we try to write to it.
  for (int attempt = 0; attempt < 2; ++attempt) {
    const bool reused = (cli_ != nullptr);
    if (!ensureSession()) { LOG.printf("[%s] connect failed\n", tag_); return ""; }

    cli_->print(req);
    cli_->print(env);

    String out;
    bool keepAlive = false;
    if (readResponse(out, keepAlive)) {
      lastUseMs_ = millis();
      if (!keepAlive) dropSession();
      return out;
    }
    dropSession();
    if (!reused) break;        // a brand-new connection failing is a real failure
  }
  return "";
}

}  // namespace smapi
