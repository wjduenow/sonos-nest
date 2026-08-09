// ESP-Hosted multicast probe for the ESP32-P4 (env: jukebox-mcast).
//
// WHY THIS EXISTS
// The P4 has no radio. Wi-Fi comes from an ESP32-C6-MINI-1 over SDIO, driven by ESP-Hosted.
// This whole product finds speakers with an SSDP **M-SEARCH to 239.255.255.250:1900** and then
// talks SOAP to them, so if multicast egress or the unicast replies don't survive the
// P4<->C6 bridge, the jukebox cannot discover anything and the architecture needs a fallback
// (cached zone IPs from settings, or an address handed over by sonos-portal).
//
// Answer this BEFORE building any UI. A device that can browse the web but can't do SSDP looks
// completely healthy right up until it finds zero speakers.
//
// WHAT IT TESTS, in order, each printed PASS/FAIL so a failure names its own layer:
//   1  association         — does the C6 link up at all (ESP-Hosted alive)
//   2  DNS + TCP egress    — ordinary unicast networking works
//   3  M-SEARCH from :1900 — THE test. Byte-for-byte the request core/sonos/ssdp.cpp sends,
//                            bound to the same source port, expecting unicast replies back.
//   4  M-SEARCH, ephemeral — same but from an OS-assigned port. If 3 fails and 4 passes, the
//                            bridge is mishandling the fixed source port, not multicast.
//   5  group join + NOTIFY — inbound multicast (Sonos announces itself periodically). An
//                            independent discovery path if M-SEARCH is dead.
//   6  HTTP to a speaker   — proves TCP to a discovered player, i.e. SOAP would work.
//
// Run:  pio run -e jukebox-mcast -t upload && python3 tools/readser.py /dev/ttyACM0 60
#ifdef JUKEBOX_MCAST_TEST

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "secrets.h"

#ifndef WIFI_SSID
#error "include/secrets.h must define WIFI_SSID / WIFI_PASS for this test"
#endif

static const IPAddress SSDP_MCAST(239, 255, 255, 250);
static const uint16_t SSDP_PORT = 1900;

// Byte-for-byte what core/sonos/ssdp.cpp sends. Do not "improve" it — the point is to test
// the real request, including MX: 1 and the ZonePlayer ST.
static const char *kMSearch =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "MAN: \"ssdp:discover\"\r\n"
    "MX: 1\r\n"
    "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
    "\r\n";

static int  s_pass = 0, s_fail = 0;
static char s_firstSpeaker[16] = {0};   // dotted IP of the first responder, for test 6

static void report(const char *name, bool ok, const char *detail = nullptr) {
  ok ? s_pass++ : s_fail++;
  Serial.printf("[%s] %-24s %s\n", ok ? "PASS" : "FAIL", name, detail ? detail : "");
}

// Pull "LOCATION: http://192.168.1.50:1400/xml/..." down to "192.168.1.50".
static String ipFromLocation(const String &resp) {
  int at = resp.indexOf("LOCATION:");
  if (at < 0) at = resp.indexOf("location:");
  if (at < 0) return "";
  int schemeEnd = resp.indexOf("://", at);
  if (schemeEnd < 0) return "";
  int hostStart = schemeEnd + 3;
  int hostEnd = hostStart;
  while (hostEnd < (int)resp.length() && resp[hostEnd] != ':' && resp[hostEnd] != '/' &&
         resp[hostEnd] != '\r' && resp[hostEnd] != '\n')
    hostEnd++;
  return resp.substring(hostStart, hostEnd);
}

// Send the M-SEARCH and collect distinct responders. `srcPort` 0 = let the stack choose.
// Returns how many unique speakers answered.
static int mSearch(uint16_t srcPort, uint32_t windowMs) {
  WiFiUDP udp;
  if (!udp.begin(srcPort)) {
    Serial.printf("      udp.begin(%u) failed\n", srcPort);
    return -1;
  }
  int found = 0;
  for (int attempt = 0; attempt < 3; ++attempt) {
    udp.beginPacket(SSDP_MCAST, SSDP_PORT);
    udp.write((const uint8_t *)kMSearch, strlen(kMSearch));
    bool sent = udp.endPacket();
    if (!sent) Serial.println("      endPacket() returned 0 — multicast egress refused");

    uint32_t deadline = millis() + windowMs;
    while ((int32_t)(deadline - millis()) > 0) {
      int len = udp.parsePacket();
      if (len > 0) {
        char buf[1024];
        int n = udp.read(buf, sizeof(buf) - 1);
        if (n > 0) {
          buf[n] = 0;
          String resp(buf);
          String ip = ipFromLocation(resp);
          if (ip.length()) {
            found++;
            Serial.printf("      responder %s (from %s:%u, %d bytes)\n", ip.c_str(),
                          udp.remoteIP().toString().c_str(), udp.remotePort(), n);
            if (!s_firstSpeaker[0]) strncpy(s_firstSpeaker, ip.c_str(), sizeof(s_firstSpeaker) - 1);
          }
        }
      }
      delay(5);
    }
    if (found) break;
  }
  udp.stop();
  return found;
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);

  Serial.println();
  Serial.println("=== sonos-jukebox — ESP-Hosted multicast / SSDP probe (ESP32-P4 + C6) ===");
  Serial.printf("[sys] chip=%s  psram=%lu KB  internal heap=%lu KB\n", ESP.getChipModel(),
                (unsigned long)(ESP.getPsramSize() / 1024),
                (unsigned long)(ESP.getFreeHeap() / 1024));

  // --- 1. association ---------------------------------------------------------
  Serial.printf("[wifi] joining \"%s\"…\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t deadline = millis() + 30000;
  while (WiFi.status() != WL_CONNECTED && (int32_t)(deadline - millis()) > 0) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    report("associate", false, "no link — ESP-Hosted / C6 firmware is the first suspect");
    Serial.println("Everything below depends on this. Stopping.");
    return;
  }
  {
    char d[96];
    snprintf(d, sizeof(d), "ip=%s gw=%s rssi=%d", WiFi.localIP().toString().c_str(),
             WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());
    report("associate", true, d);
  }

  // --- 2. ordinary unicast egress ---------------------------------------------
  {
    IPAddress dns;
    bool ok = WiFi.hostByName("www.google.com", dns);
    report("dns lookup", ok, ok ? dns.toString().c_str() : "resolver unreachable");
  }

  // --- 3. THE test: M-SEARCH from the fixed :1900 source port ------------------
  Serial.println("[ssdp] M-SEARCH from source port 1900 (exactly what ssdp.cpp does)…");
  int n1900 = mSearch(SSDP_PORT, 1500);
  {
    char d[64];
    snprintf(d, sizeof(d), "%d responder(s)", n1900 < 0 ? 0 : n1900);
    report("M-SEARCH :1900", n1900 > 0, d);
  }

  // --- 4. same, but from an ephemeral source port ------------------------------
  Serial.println("[ssdp] M-SEARCH from an ephemeral source port…");
  int nEph = mSearch(0, 1500);
  {
    char d[64];
    snprintf(d, sizeof(d), "%d responder(s)", nEph < 0 ? 0 : nEph);
    report("M-SEARCH ephemeral", nEph > 0, d);
  }
  if (n1900 <= 0 && nEph > 0)
    Serial.println("      >>> multicast egress is FINE; binding source port 1900 is what breaks.");

  // --- 5. inbound multicast: listen for Sonos NOTIFY announcements -------------
  // Independent of M-SEARCH. Sonos re-announces every ~30 min, so a miss here is weak
  // evidence either way — but a hit proves group membership works.
  Serial.println("[ssdp] joining 239.255.255.250 and listening 10 s for NOTIFY…");
  {
    WiFiUDP mudp;
    bool joined = mudp.beginMulticast(SSDP_MCAST, SSDP_PORT);
    int notifies = 0;
    if (joined) {
      uint32_t until = millis() + 10000;
      while ((int32_t)(until - millis()) > 0) {
        int len = mudp.parsePacket();
        if (len > 0) {
          char buf[512];
          int n = mudp.read(buf, sizeof(buf) - 1);
          if (n > 0) {
            buf[n] = 0;
            if (strncmp(buf, "NOTIFY", 6) == 0) {
              notifies++;
              Serial.printf("      NOTIFY from %s\n", mudp.remoteIP().toString().c_str());
            }
          }
        }
        delay(5);
      }
      mudp.stop();
    }
    char d[64];
    snprintf(d, sizeof(d), "join=%s notifies=%d", joined ? "ok" : "FAILED", notifies);
    // Only the join is a hard requirement; NOTIFY traffic is opportunistic.
    report("multicast group join", joined, d);
  }

  // --- 6. TCP to a real speaker ------------------------------------------------
  if (s_firstSpeaker[0]) {
    WiFiClient c;
    bool ok = c.connect(s_firstSpeaker, 1400);
    if (ok) {
      c.printf("GET /xml/device_description.xml HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
               s_firstSpeaker);
      String status = c.readStringUntil('\n');
      status.trim();
      c.stop();
      report("HTTP to speaker", status.indexOf("200") > 0, status.c_str());
    } else {
      report("HTTP to speaker", false, "TCP connect to :1400 refused");
    }
  } else {
    Serial.println("[skip] no speaker discovered, so the SOAP-path test can't run.");
  }

  // --- verdict -----------------------------------------------------------------
  Serial.println();
  Serial.printf("=== %d passed, %d failed ===\n", s_pass, s_fail);
  if (n1900 > 0) {
    Serial.println("VERDICT: SSDP discovery works over ESP-Hosted. core/sonos/ssdp.cpp should");
    Serial.println("         port across unchanged. Proceed with the jukebox as planned.");
  } else if (nEph > 0) {
    Serial.println("VERDICT: multicast works, but NOT from source port 1900. ssdp.cpp binds 1900");
    Serial.println("         (udp.begin(1900)); switching it to an ephemeral port is a one-line");
    Serial.println("         fix and is legal SSDP — replies come back to whatever port we sent from.");
  } else {
    Serial.println("VERDICT: no SSDP responses over ESP-Hosted. Discovery needs a fallback —");
    Serial.println("         cached zone IPs in settings, or an address from sonos-portal.");
    Serial.println("         Re-run near the speakers and confirm the AP isn't blocking multicast");
    Serial.println("         (client isolation / IGMP snooping) before blaming the C6 bridge.");
  }
}

void loop() { delay(1000); }

#endif  // JUKEBOX_MCAST_TEST
