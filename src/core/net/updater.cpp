// See updater.h. HTTP pull-OTA against the plans/06 manifest schema.
#include "updater.h"
#include "../settings.h"   // settingsUpdateUrl() / settingsOtaAuto()
#include "ota.h"           // otaHostname() — the stable id we send so the portal can target us
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include "logmirror.h"   // LOG — tees to the TCP mirror where enabled, plain Serial otherwise

// Firmware version — injected per build by tools/git_version.py (git describe). The manifest's
// version is string-compared against this: CI only publishes clean tags, so a device on the
// blessed release reads equal (no update), and anything else (older tag, dirty dev build) differs.
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// Compiled-in default update source when a device has no portal and no explicit pick: the repo's
// latest GitHub Release. The `latest/download/<asset>` form is stable and auto-follows every new
// release, so no version is baked in. Overridable per fork via -DUPDATE_MANIFEST_URL.
#ifndef UPDATE_MANIFEST_URL
#define UPDATE_MANIFEST_URL "https://github.com/wjduenow/sonos-nest/releases/latest/download/manifest.json"
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

// Resolve the effective manifest source (see updater.h). kindOut, if given, gets a static label.
static String resolveUrl(const char **kindOut) {
  String stored = settingsUpdateUrl();
  if (stored == "off")     { if (kindOut) *kindOut = "off";    return String(); }        // disabled
  if (stored.length())     { if (kindOut) *kindOut = "custom"; return stored; }           // explicit
  String portal = settingsPortal();                                                       // "ip:port" or ""
  if (portal.length())     { if (kindOut) *kindOut = "portal"; return "http://" + portal + "/api/firmware"; }
  if (kindOut) *kindOut = "github";                                                        // auto → GitHub default
  return String(UPDATE_MANIFEST_URL);
}

String      updaterEffectiveUrl() { return resolveUrl(nullptr); }
const char *updaterSourceKind()   { const char *k = "off"; resolveUrl(&k); return k; }

// Fetch the manifest at `base` and pull out this unit's target. Appends our identity as query
// params so a portal can answer per-device (a static GitHub manifest just ignores them, and drops
// them across its redirect — harmless). Returns true and fills version/url/approved when present.
static bool checkManifest(const String &base, String &version, String &url, bool &approved) {
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
  LOG.printf("[updater] applying %s\n           from %s\n", s_available.c_str(), url.c_str());

  // Quiesce the rest of the system BEFORE any flash write. Flash writes disable the instruction
  // cache, so any other-core task executing from (uncached) flash during a write faults and the
  // device resets mid-download — exactly the espota hazard uiTask/artTask already dodge via
  // otaActive(). Set the flag, then give those tasks a beat to reach their backoff delay (their
  // longest loop period is ~200 ms) before HTTPUpdate starts erasing.
  s_active = true;
  vTaskDelay(pdMS_TO_TICKS(400));

  // HTTPUpdate's read/write loop never yields, so this task (netTask, core 0) starves IDLE0 for
  // the whole download and the Task WDT (~5 s) resets the device mid-transfer — the crash the
  // hardware test hit (reset reason 6 = TASK_WDT). espota dodges it because ArduinoOTA yields in
  // its loop. delay(1) per progress chunk lets IDLE0 run (feeding the WDT); ~400 chunks over a
  // 1.6 MB image adds well under a second.
  httpUpdate.onProgress([](int cur, int total) {
    static int lastPct = -1;
    delay(1);
    int pct = total ? (int)((int64_t)cur * 100 / total) : 0;
    if (pct != lastPct && pct % 10 == 0) { lastPct = pct; LOG.printf("[updater] %d%%\n", pct); }
  });
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
    LOG.printf("[updater] FAILED (%d) %s\n", httpUpdate.getLastError(),
                  httpUpdate.getLastErrorString().c_str());
  else if (r == HTTP_UPDATE_NO_UPDATES)
    LOG.println("[updater] server reports no update");
}

// Parse "vX.Y.Z[-N[-gHASH[-dirty]]]" (git describe form) into [major,minor,patch,commits]. The
// commit count N (commits past the tag) is the 4th field so a post-tag dev build sorts *newer*
// than its tag. Returns false if the string doesn't start with a numeric version (e.g. a bare
// commit hash), so the caller can fall back.
static bool parseVer(const String &s, long v[4]) {
  v[0] = v[1] = v[2] = v[3] = 0;
  int i = 0, n = s.length();
  if (i < n && (s[i] == 'v' || s[i] == 'V')) i++;
  if (i >= n || !isdigit((int)s[i])) return false;
  for (int field = 0; field < 3 && i < n;) {
    long num = 0; bool got = false;
    while (i < n && isdigit((int)s[i])) { num = num * 10 + (s[i] - '0'); i++; got = true; }
    if (!got) break;
    v[field++] = num;
    if (i < n && s[i] == '.') i++; else break;
  }
  if (i < n && s[i] == '-') {          // "-N" commit count after the tag
    i++;
    long num = 0; bool got = false;
    while (i < n && isdigit((int)s[i])) { num = num * 10 + (s[i] - '0'); i++; got = true; }
    if (got) v[3] = num;
  }
  return true;
}

// True iff `cand` is STRICTLY newer than `cur`. This is what stops the updater from ever offering
// or applying an equal-or-older build — the downgrade-loop that reverted the nest when a lagging
// mirror served an older release with otaAuto on. Falls back to `cand != cur` only if a version
// isn't a parseable vX.Y.Z (a bare-hash dev build), so such a device can still take a real release.
static bool isNewer(const String &cand, const String &cur) {
  long a[4], b[4];
  if (!parseVer(cand, a) || !parseVer(cur, b)) return cand != cur;
  for (int k = 0; k < 4; k++) if (a[k] != b[k]) return a[k] > b[k];
  return false;   // equal
}

// Core check. applyAuto=true only at boot, so an otaAuto device applies pre-playback rather than
// mid-run (see the policy note in updater.h).
static void run(bool applyAuto) {
  if (WiFi.status() != WL_CONNECTED) return;
  String base = updaterEffectiveUrl();
  if (base.length() == 0) { s_available = ""; return; }  // "off": checking disabled

  String version, url;
  bool approved = false;
  if (!checkManifest(base, version, url, approved)) return;   // source down / bad manifest: keep prior state

  if (!isNewer(version, FW_VERSION)) { s_available = ""; return; }  // equal/older → nothing to offer
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
