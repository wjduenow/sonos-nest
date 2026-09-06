// SMAPI transport — the Sonos Music API over SOAP, one keep-alive TLS session per service.
//
// WHY THIS EXISTS SEPARATELY FROM amazon.cpp. Sonos cannot tell a controller anything about a music
// service's catalogue (the local ContentDirectory returns UPnP 701 for every third-party container,
// and the household's own tokens are stored on the players write-only), so a unit that wants to
// browse or SEARCH a service has to become its own registered account and talk SMAPI itself. That
// is not an Amazon-shaped problem: `plans/08` establishes that the same anonymous `getAppLink`
// ceremony works on Spotify, Pandora, TuneIn, Plex and Audible too. Everything in here is the part
// that does not care which service it is talking to.
//
// What is NOT here, on purpose: the link ceremony's task and state, and the NVS keys the resulting
// token lands in. Both are per-service and each service owns its own — see `spotify.cpp` and
// `amazon.cpp`. Generalising them behind callbacks bought less than it cost.
//
// EVERYTHING HERE IS BLOCKING HTTPS. Call it from netTask or a dedicated task, never from uiTask.
#pragma once

#include <Arduino.h>
// Included rather than forward-declared, and it has to be: on Arduino 3.x (the ESP32-P4 jukebox)
// WiFiClientSecure is a TYPEDEF for NetworkClientSecure, not a class, so `class WiFiClientSecure;`
// is a hard conflicting-declaration error there while compiling fine on the S3 units' 2.0.17. Same
// 2.x/3.x split that needed a shim in core/net/registrar.cpp.
#include <WiFiClientSecure.h>

namespace smapi {

// The SMAPI namespace, which is also the SOAPAction prefix.
extern const char *kNs;

// --- XML helpers ---------------------------------------------------------------------------------
// Lifted verbatim from amazon.cpp, where each one was established by a false negative first.

// Value of the first <tag>...</tag>, matched on the BARE LOCAL NAME so a namespace prefix
// (<ns:authToken>) and attributes (<albumArtURI requiresAuthentication="false">) both work. Both
// forms occur in real responses and both silently defeat a naive `<tag>` match. Returns "" when
// absent; callers that care must check emptiness rather than trusting a default.
String tagValue(const String &xml, const char *tag, int from = 0);

String unescapeXml(String s);
String escapeXml(const String &in);

// SMAPI object ids go into a URI percent-encoded. '/' and '#' are the ones that matter.
String urlEncode(const String &in);

// --- credentials ---------------------------------------------------------------------------------

// The header the pre-auth legs send. It carries nothing, but an omitted <s:Header> is a 500 (WCF
// parse fault) so it is still required.
//
// ⚠️ The household id does NOT go in here. The WSDL puts it in <credentials><deviceId> and gives
// getAppLink only hardware/osVersion/sonosAppName/callbackPath, but Amazon reads `householdId`
// from the request BODY and answers the schema-correct form with a hard
// `400 "householdId must not be blank or null!"`. Do not "fix" a caller to match the schema.
String anonCreds();

// The authenticated header: an account token this device owns, obtained from the link ceremony.
String loginCreds(const String &token, const String &key, const String &householdId);

// c_str() that can never be NULL.
//
// ⚠️ NOT PARANOIA, AND IT HAS EARNED ITS KEEP TWICE. A String reads back with a NULL buffer in two
// situations: Arduino calls invalidate() when an allocation fails, and a String read through a
// dangling reference can look the same. Passing either to a %s is a load from address 0 inside ROM
// strlen.
//
// Both happened here. A LOG.printf added to diagnose an empty browse turned the out-of-memory it
// was diagnosing into a reboot; and later "(null)" in that same log line is what identified a
// use-after-free in the Radio page, where the container id had been freed before the request was
// built. Any %s of a String that came from the network goes through this.
inline const char *cstr(const String &s) { return s.c_str() ? s.c_str() : "(null)"; }

// --- one service endpoint ------------------------------------------------------------------------

// A SINGLE KEEP-ALIVE TLS SESSION, reused across every call to one service.
//
// A fresh WiFiClientSecure per request costs a TCP connect, a TLS handshake (measured 0.5-1.4 s)
// and a close every time — most of a crawl's wall clock, and exactly the connect/close churn that
// wedges the jukebox's ESP-Hosted link (plans/07).
//
// ⚠️ ONE SESSION AT A TIME ACROSS ALL SERVICES. mbedTLS is configured to allocate from PSRAM on the
// jukebox (`MBEDTLS_EXTERNAL_MEM_ALLOC`), which is load-bearing, but a second concurrent session
// still costs internal SRAM — the resource that breaks first on every board here. Call endSession()
// when a burst finishes rather than leaving two services holding sockets.
class Client {
 public:
  // `host` is the TLS host and the Host: header. `path` is the request target — services differ
  // ("/" for Amazon, "/smapi" for Spotify), and posting to the wrong one is a 404 that looks like a
  // service outage. `logTag` prefixes this client's log lines and its heap-watch tag.
  Client(const char *host, const char *path, const char *logTag);

  // One SOAP round trip. `header` is the full <s:Header> contents. Returns the body, or "" on
  // transport failure. HTTP status is deliberately not distinguished: a SOAP fault arrives as a 500
  // with a body the caller still needs, so the caller inspects the body either way.
  String post(const String &action, const String &header, const String &body);

  // Drop the pooled session. Call when a burst of requests is finished — the crawl does — so an
  // idle socket is not held on a link with few to spare. Harmless if none is open.
  void endSession();

 private:
  bool ensureSession();
  void dropSession();
  bool readResponse(String &out, bool &keepAlive);

  const char      *host_;
  const char      *path_;
  const char      *tag_;
  String           bodyTag_;              // "<tag>.body", for heapwatch::note
  WiFiClientSecure *cli_       = nullptr;
  uint32_t          lastUseMs_ = 0;
};

}  // namespace smapi
