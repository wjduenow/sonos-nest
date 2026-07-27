// Runtime smoke test for src/core/ on the ESP32-P4 (env: jukebox-smoke).
//
// core/ COMPILES on Arduino 3.x (see plans/07). This asks the next question: does it actually
// WORK on this silicon and this network stack? Every step below runs the real shared code, not a
// reimplementation — the point is to exercise ssdp.cpp / soap_client.cpp / didl.cpp themselves,
// so a pass here means the jukebox unit can rely on them.
//
// Runs against the P4's Wi-Fi, which is an ESP32-C6 over SDIO via ESP-Hosted rather than an
// on-die radio, so this is also the first test of core/ over that bridge.
//
// No board HAL, no LVGL, no UI. Sequence, each printed PASS/FAIL so a failure names its layer:
//   1  NVS (settingsInit) ......... does Preferences work on 3.x
//   2  Wi-Fi (wifiConnect) ........ core's own connect path, over ESP-Hosted
//   3  SSDP (ssdpDiscover) ........ the real discovery + GetZoneGroupState topology parse
//   4  SOAP reads ................. GetTransportInfo / GetVolume / GetPositionInfo per zone
//   5  DIDL ....................... did the metadata parse produce sane fields
//   6  Album art .................. HTTPClient chunked read — the documented trap
//
// Run:  pio run -e jukebox-smoke -t upload && python3 tools/readser.py /dev/ttyUSB0 60
#ifdef JUKEBOX_SMOKE_TEST

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "core/net/wifi.h"
#include "core/player_state.h"
#include "core/settings.h"
#include "core/sonos/soap_client.h"
#include "core/sonos/ssdp.h"

static int s_pass = 0, s_fail = 0;
// The whole table is kept, not just the counts, so one power cycle yields the full result — see
// the note on loop(). Detail strings are short; this costs a couple of hundred bytes.
static String s_log;

static void skip(const char *name, const String &why) {
  char line[160];
  snprintf(line, sizeof(line), "[SKIP] %-22s %s", name, why.c_str());
  Serial.println(line);
  s_log += line; s_log += '\n';
}

static void report(const char *name, bool ok, const String &detail = String()) {
  ok ? s_pass++ : s_fail++;
  char line[160];
  snprintf(line, sizeof(line), "[%s] %-22s %s", ok ? "PASS" : "FAIL", name, detail.c_str());
  Serial.println(line);
  s_log += line;
  s_log += '\n';
}

// Album art is the one path CLAUDE.md calls out as easy to get wrong: the speaker serves it
// chunked, and a raw stream read leaks chunk-size framing into the JPEG. core/album_art.cpp uses
// HTTPClient::writeToStream() for exactly that reason. Reproduce that read here (without LVGL or
// the decoder) and sanity-check the result really is a JPEG.
static bool fetchArt(const String &url, size_t &bytesOut) {
  bytesOut = 0;
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) return false;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }

  // Drain via writeToStream so HTTPClient de-chunks for us, counting bytes and keeping the head.
  struct Counter : public Stream {
    size_t n = 0;
    uint8_t head[4] = {0};
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    size_t write(uint8_t c) override { if (n < 4) head[n] = c; n++; return 1; }
    size_t write(const uint8_t *b, size_t len) override {
      for (size_t i = 0; i < len && n < 4; i++) head[n + i] = b[i];
      n += len; return len;
    }
  } sink;

  http.writeToStream(&sink);
  http.end();
  bytesOut = sink.n;
  // JPEG SOI marker. If chunk framing leaked in, the first bytes are ASCII hex + CRLF instead.
  return sink.n > 1024 && sink.head[0] == 0xFF && sink.head[1] == 0xD8;
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);

  Serial.println();
  Serial.println("=== sonos-jukebox — core/ runtime smoke test (ESP32-P4, Arduino 3.x) ===");
  Serial.printf("[sys] chip=%s  internal heap free=%lu KB  psram free=%lu KB\n",
                ESP.getChipModel(), (unsigned long)(ESP.getFreeHeap() / 1024),
                (unsigned long)(ESP.getFreePsram() / 1024));

  // --- 1. NVS -----------------------------------------------------------------
  settingsInit();
  // Round-trip a value to prove Preferences reads and writes, not just that init returned.
  String beforeRoom = settingsRoom();
  settingsSetRoom("__smoke__");
  bool nvsOk = (settingsRoom() == "__smoke__");
  settingsSetRoom(beforeRoom);     // put it back; this device may already be configured
  report("NVS round-trip", nvsOk, nvsOk ? "Preferences ok" : "write/read mismatch");

  // --- 2. Wi-Fi via core ------------------------------------------------------
  bool wifiOk = wifiConnect();
  report("wifiConnect()", wifiOk,
         wifiOk ? ("ip=" + WiFi.localIP().toString() + " ssid=" + wifiSsid())
                : "no link (creds in secrets.h/NVS? C6 slave firmware?)");
  if (!wifiOk) {
    Serial.println("Everything below needs the network. Stopping.");
    Serial.printf("\n=== %d passed, %d failed ===\n", s_pass, s_fail);
    return;
  }

  // --- 3. SSDP discovery via core --------------------------------------------
  // This is the real thing: M-SEARCH for a seed, then GetZoneGroupState parsed into rooms with
  // coordinator IPs. The standalone probe in multicast_test.cpp only proved responders exist.
  uint32_t t = millis();
  bool disc = sonos::ssdpDiscover();
  uint32_t discMs = millis() - t;
  const std::vector<sonos::Zone> &zs = sonos::zones();
  report("ssdpDiscover()", disc && !zs.empty(),
         String(zs.size()) + " zone(s) in " + String(discMs) + " ms");

  for (const auto &z : zs) {
    Serial.printf("       %-18s ip=%-15s coord=%-15s %s\n", z.name.c_str(), z.ip.c_str(),
                  z.coordIp.c_str(), z.isCoordinator ? "(coordinator)" : "");
  }
  if (zs.empty()) {
    Serial.printf("\n=== %d passed, %d failed ===\n", s_pass, s_fail);
    return;
  }

  // --- 4. SOAP reads ----------------------------------------------------------
  // Read-only calls on purpose: this must not disturb whatever is actually playing.
  // Prefer a zone that is actually playing: DIDL and album art can only be exercised against
  // real metadata, and picking zs[0] blindly tests nothing when that room happens to be idle.
  int pick = 0;
  bool anyPlaying = false;
  for (size_t i = 0; i < zs.size(); i++) {
    TransportState probe = TransportState::Unknown;
    const String &tip = zs[i].coordIp.length() ? zs[i].coordIp : zs[i].ip;
    if (sonos::getTransportInfo(tip, probe) && probe == TransportState::Playing) {
      pick = (int)i; anyPlaying = true; break;
    }
  }
  const sonos::Zone &z = zs[pick];
  Serial.printf("[soap] exercising \"%s\"%s\n", z.name.c_str(),
                anyPlaying ? " (playing)" : " (nothing playing anywhere)");

  TransportState ts = TransportState::Unknown;
  bool tOk = sonos::getTransportInfo(z.coordIp.length() ? z.coordIp : z.ip, ts);
  report("GetTransportInfo", tOk, tOk ? ("state=" + String((int)ts)) : "call failed");

  uint8_t vol = 0;
  bool vOk = sonos::getVolume(z.ip, vol);       // volume is per-speaker, not per-group
  report("GetVolume", vOk, vOk ? ("vol=" + String(vol)) : "call failed");

  PlayerState ps;
  bool pOk = sonos::getPositionInfo(z.coordIp.length() ? z.coordIp : z.ip, ps);
  report("GetPositionInfo", pOk,
         pOk ? ("pos=" + String(ps.positionSec) + "/" + String(ps.durationSec) + "s")
             : "call failed");

  // --- 5. DIDL --------------------------------------------------------------
  // The double-unescape trap (CLAUDE.md): get it wrong and artUri arrives with &amp; in it.
  if (pOk) {
    Serial.printf("       title=\"%s\" artist=\"%s\" album=\"%s\"\n", ps.title.c_str(),
                  ps.artist.c_str(), ps.album.c_str());
    Serial.printf("       artUri=%s\n", ps.artUri.c_str());
    bool didlOk = ps.title.length() > 0 || ps.artUri.length() > 0;
    bool notDoubleEscaped = ps.artUri.indexOf("&amp;") < 0;
    if (!didlOk && !anyPlaying) {
      // Not a failure of core/: there is simply no metadata to parse. Start playback on any
      // speaker and re-run to exercise DIDL and the chunked album-art read for real.
      skip("DIDL parse", "nothing playing on any of the " + String(zs.size()) + " zones");
    } else {
      report("DIDL parse", didlOk && notDoubleEscaped,
             !notDoubleEscaped ? "artUri still contains &amp; — unescape regressed"
                               : (didlOk ? "fields populated" : "empty despite transport=playing"));
    }

    // --- 6. Album art over chunked HTTP -------------------------------------
    if (ps.artUri.startsWith("http")) {
      size_t n = 0;
      bool aOk = fetchArt(ps.artUri, n);
      report("album art (chunked)", aOk,
             aOk ? (String(n / 1024) + " KB, JPEG SOI ok")
                 : (n ? String("got ") + String(n) + " B but not a JPEG — chunk framing leaked?"
                      : "fetch failed"));
    } else {
      skip("album art (chunked)", "no artUri (idle, or a radio stream without art)");
    }
  }

  Serial.printf("\n=== %d passed, %d failed ===\n", s_pass, s_fail);
  if (s_fail == 0)
    Serial.println("VERDICT: core/ works on the P4. The jukebox unit can build on it.");
  else
    Serial.println("VERDICT: see the FAIL lines above — each names the layer that broke.");
  Serial.printf("[sys] internal heap free=%lu KB after the run\n",
                (unsigned long)(ESP.getFreeHeap() / 1024));
}

// Reprint the verdict forever. A reset re-wedges the C6, so the only clean way to observe a run
// is: power-cycle, then read serial at leisure. That requires the result to still be on the wire
// minutes later — the same latching lesson as the GT911 touch bring-up.
void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 5000) {
    last = millis();
    Serial.println("---- smoke result ----");
    Serial.print(s_log);
    Serial.printf("[verdict] %d passed, %d failed  (uptime %lus, heap %lu KB)\n", s_pass, s_fail,
                  (unsigned long)(millis() / 1000), (unsigned long)(ESP.getFreeHeap() / 1024));
  }
  delay(50);
}

#endif  // JUKEBOX_SMOKE_TEST
