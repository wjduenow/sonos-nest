// See updater.h. HTTP pull-OTA against the plans/06 manifest schema.
#include "updater.h"
#include "../settings.h"   // settingsUpdateUrl() / settingsOtaAuto()
#include "ota.h"           // otaHostname() — the stable id we send so the portal can target us
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

// Firmware version — injected per build by tools/git_version.py (git describe). The manifest's
// version is string-compared against this: CI only publishes clean tags, so a device on the
// blessed release reads equal (no update), and anything else (older tag, dirty dev build) differs.
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

static const uint32_t kCheckMs = 6UL * 60 * 60 * 1000;  // 6 h between periodic checks

static String   s_available;             // available version, "" if none / up-to-date / disabled
static bool     s_armed    = false;      // explicit approve → apply on the next check
static bool     s_force    = false;      // bypass the rate limit once (url changed / approve)
static uint32_t s_lastCheck = 0;
static volatile bool s_active = false;   // a pull-flash is running (UI/art must quiesce)

bool   updaterActive()           { return s_active; }
bool   updaterAvailable()        { return s_available.length() > 0; }
String updaterAvailableVersion() { return s_available; }
void   updaterApprove()          { s_armed = true; s_force = true; }
void   updaterForceCheck()       { s_force = true; }

// This unit's manifest key — the same id registrationJson() reports, from the env's build macro.
static const char *unitId() {
#if defined(UNIT_NEST)
  return "nest";
#elif defined(UNIT_SLEEP)
  return "sleep";
#elif defined(HEADLESS)
  return "button";
#else
  return "unknown";
#endif
}

// GET a (small) body over HTTP or HTTPS. HTTPS (a GitHub-hosted manifest) uses an insecure TLS
// client — LAN/hobby threat model, and the portal path is plain HTTP so it never pays for this.
// Follows redirects because GitHub release/asset URLs 302 to objects.githubusercontent.com.
// Templated on the client type: begin() wants a concrete WiFiClient& (WiFiClientSecure derives from
// it), and the screen-less envs compile as gnu++11 with no generic lambdas — so a small template,
// not a Client& lambda.
template <typename C>
static bool doGet(HTTPClient &http, C &client, const String &url, String &body) {
  if (!http.begin(client, url)) return false;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  int code = http.GET();
  bool ok = (code == HTTP_CODE_OK);
  if (ok) body = http.getString();
  http.end();
  return ok;
}

static bool httpGetString(const String &url, String &body) {
  HTTPClient http;
  if (url.startsWith("https:")) {
    WiFiClientSecure sec;
    sec.setInsecure();
    return doGet(http, sec, url, body);
  }
  WiFiClient cl;
  return doGet(http, cl, url, body);
}

// Fetch the manifest and pull out this unit's target. Appends our identity as query params so a
// portal can answer per-device (a static GitHub manifest just ignores them). Returns true and
// fills version/url/approved when a target for this unit is present.
static bool checkManifest(String &version, String &url, bool &approved) {
  String base = settingsUpdateUrl();
  if (base.length() == 0) return false;

  String q = base;
  q += (base.indexOf('?') >= 0) ? '&' : '?';
  q += "id=";   q += otaHostname(); q += ".local";
  q += "&fw=";  q += FW_VERSION;     // controlled, URL-safe (letters/digits/./-)
  q += "&unit="; q += unitId();

  String body;
  if (!httpGetString(q, body)) return false;

  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;   // truthy DeserializationError == parse failed

  JsonObject u = doc["units"][unitId()];
  if (u.isNull()) return false;
  const char *urlp = u["url"];
  if (!urlp) return false;

  version  = doc["version"] | "";
  url      = urlp;
  approved = u["approved"] | false;
  return version.length() > 0;
}

// Download + flash the given URL. Blocks the calling task for the transfer; on success the device
// reboots into the new slot and this never returns. A failed/partial download leaves the running
// firmware untouched (HTTPUpdate writes the inactive OTA slot and only flips on a clean image).
static void applyNow(const String &url) {
  s_armed = false;
  Serial.printf("[updater] applying %s\n           from %s\n", s_available.c_str(), url.c_str());

  // Quiesce the rest of the system BEFORE any flash write. Flash writes disable the instruction
  // cache, so any other-core task executing from (uncached) flash during a write faults and the
  // device resets mid-download — exactly the espota hazard uiTask/artTask already dodge via
  // otaActive(). Set the flag, then give those tasks a beat to reach their backoff delay (their
  // longest loop period is ~200 ms) before HTTPUpdate starts erasing.
  s_active = true;
  vTaskDelay(pdMS_TO_TICKS(400));

  httpUpdate.rebootOnUpdate(true);

  t_httpUpdate_return r;
  if (url.startsWith("https:")) {
    WiFiClientSecure sec;
    sec.setInsecure();
    r = httpUpdate.update(sec, url, FW_VERSION);
  } else {
    WiFiClient cl;
    r = httpUpdate.update(cl, url, FW_VERSION);
  }

  // Only reached on failure — HTTP_UPDATE_OK reboots. Clear the flag so the UI/art tasks resume.
  s_active = false;
  if (r == HTTP_UPDATE_FAILED)
    Serial.printf("[updater] FAILED (%d) %s\n", httpUpdate.getLastError(),
                  httpUpdate.getLastErrorString().c_str());
  else if (r == HTTP_UPDATE_NO_UPDATES)
    Serial.println("[updater] server reports no update");
}

// Core check. applyAuto=true only at boot, so an otaAuto device applies pre-playback rather than
// mid-run (see the policy note in updater.h).
static void run(bool applyAuto) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (settingsUpdateUrl().length() == 0) { s_available = ""; return; }  // opt-out: feature dormant

  String version, url;
  bool approved = false;
  if (!checkManifest(version, url, approved)) return;   // portal down / bad manifest: keep prior state

  if (version == FW_VERSION) { s_available = ""; return; }  // up to date
  s_available = version;

  if (s_armed || approved || (applyAuto && settingsOtaAuto()))
    applyNow(url);   // reboots on success
}

void updaterBegin() {
  run(true);
  s_lastCheck = millis();
}

void updaterTick() {
  if (!s_force && (millis() - s_lastCheck < kCheckMs)) return;
  s_force = false;
  s_lastCheck = millis();
  run(false);
}
