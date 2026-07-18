// See portal.h. SoftAP captive portal for headless WiFi provisioning.
//
// Only compiled into HEADLESS builds (the sonos-button): a unit with a screen provisions WiFi
// through its own on-screen UX, and gating here keeps WebServer/DNSServer out of those binaries.
#ifdef HEADLESS

#include "portal.h"
#include "wifi.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

namespace {

const uint16_t HTTP_PORT = 80;   // MUST be 80: phones probe http://<gateway>/ there to decide
                                 // whether to pop the captive-portal sheet. :8080 never gets hit.
const uint16_t DNS_PORT  = 53;

WebServer *s_server = nullptr;
DNSServer *s_dns    = nullptr;
IPAddress  s_apIp;

// Minimal HTML-escape for SSIDs rendered into the page (an SSID can contain & < > ").
String esc(const String &in) {
  String o; o.reserve(in.length() + 8);
  for (char c : in) {
    switch (c) {
      case '&': o += "&amp;";  break;
      case '<': o += "&lt;";   break;
      case '>': o += "&gt;";   break;
      case '"': o += "&quot;"; break;
      default:  o += c;
    }
  }
  return o;
}

const char kHead[] PROGMEM =
  "<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\">"
  "<title>Sonos Button setup</title><style>"
  ":root{color-scheme:light dark}"
  "body{font:16px/1.5 system-ui,sans-serif;margin:0;padding:24px;max-width:26rem;margin-inline:auto}"
  "h1{font-size:1.25rem;margin:0 0 1.25rem}"
  "form{border:1px solid #8883;border-radius:12px;padding:16px}"
  "label{display:block;font-weight:600;margin:0 0 1rem}"
  "select,input{font:inherit;width:100%;padding:.5rem;border-radius:8px;box-sizing:border-box;margin-top:.3rem}"
  "button{font:inherit;padding:.6rem 1.2rem;border-radius:8px;border:1px solid #2a7;background:#2a7;color:#fff;cursor:pointer}"
  ".hint{font-size:.85rem;opacity:.75;margin-top:1rem}"
  "</style>";

// The join form, with the live scan results inlined as <option>s. Server-rendered (not fetch)
// so it works in the stripped-down captive-portal browser that Android/iOS pop, which can't be
// trusted to run JS.
String pageIndex() {
  int n = WiFi.scanNetworks();   // synchronous ~1-2 s; fine for a one-off setup page
  String opts;
  for (int i = 0; i < n; ++i) {
    const String ss = WiFi.SSID(i);
    if (!ss.length()) continue;                 // skip hidden/blank
    opts += "<option value=\"" + esc(ss) + "\">" + esc(ss);
    opts += WiFi.RSSI(i) >= -67 ? "</option>" : " &middot;</option>";   // &middot; = weaker signal
  }
  WiFi.scanDelete();
  if (opts.isEmpty()) opts = "<option value=\"\">(no networks found — refresh)</option>";

  String p = FPSTR(kHead);
  p += "<h1>Sonos Button &mdash; Wi-Fi setup</h1>";
  p += "<form method=POST action=/join>";
  p += "<label>Network<select name=ssid>" + opts + "</select></label>";
  p += "<label>Password<input type=password name=pass autocapitalize=off autocorrect=off "
       "spellcheck=false></label>";
  p += "<button>Join</button></form>";
  p += "<p class=hint>Pick your Wi-Fi and enter its password. The button will join it, this "
       "setup network will disappear, and you can close this page.</p>";
  return p;
}

String pageResult(const String &title, const String &body, bool retry) {
  String p = FPSTR(kHead);
  p += "<h1>" + title + "</h1><p>" + body + "</p>";
  if (retry) p += "<p class=hint><a href=\"/\">&larr; back to setup</a></p>";
  return p;
}

void handleIndex() { s_server->send(200, "text/html", pageIndex()); }

// POST /join — apply the picked creds. wifiApply() blocks ~12 s: it tries them, persists on
// success and reverts on failure, so a wrong password lands us right back here, still in AP mode.
void handleJoin() {
  const String ssid = s_server->arg("ssid");
  const String pass = s_server->arg("pass");
  if (ssid.isEmpty()) {
    s_server->send(200, "text/html",
                   pageResult("Pick a network", "No Wi-Fi network was selected.", true));
    return;
  }
  Serial.printf("[portal ] joining \"%s\"...\n", ssid.c_str());
  wifiApplyResultReset();
  wifiApply(ssid, pass);   // AP stays up (disconnect() drops STA only), so we can still respond
  if (wifiApplyResult() == WIFI_APPLY_OK) {
    Serial.printf("[portal ] joined \"%s\" as %s\n", ssid.c_str(),
                  WiFi.localIP().toString().c_str());
    s_server->send(200, "text/html",
                   pageResult("Connected",
                              "Joined <b>" + esc(ssid) + "</b> at " +
                                  WiFi.localIP().toString() +
                                  ". The setup network is closing.",
                              false));
    // portalRun()'s loop sees wifiIsConnected() next tick and tears the AP down.
  } else {
    Serial.printf("[portal ] join failed for \"%s\"\n", ssid.c_str());
    s_server->send(200, "text/html",
                   pageResult("Couldn't join",
                              "Couldn't connect to <b>" + esc(ssid) +
                                  "</b> &mdash; check the password and try again.",
                              true));
  }
}

// Any other path (the OS captivity probes: /generate_204, /hotspot-detect.html, /ncsi.txt, …)
// redirects to the join page. Combined with the wildcard DNS below, that pops the captive sheet.
void handleCaptive() {
  s_server->sendHeader("Location", "http://" + s_apIp.toString() + "/", true);
  s_server->send(302, "text/plain", "");
}

}  // namespace

void portalRun(const char *apSsid) {
  Serial.printf("[portal ] no usable WiFi — raising setup AP \"%s\"\n", apSsid);

  // AP_STA so we can scan real networks (WiFi.scanNetworks) while serving the AP.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid);                 // open network
  delay(100);
  s_apIp = WiFi.softAPIP();            // 192.168.4.1
  Serial.printf("[portal ] join \"%s\", then open http://%s/\n", apSsid,
                s_apIp.toString().c_str());

  s_dns = new DNSServer();
  s_dns->start(DNS_PORT, "*", s_apIp); // wildcard: every hostname resolves to us

  s_server = new WebServer(HTTP_PORT);
  s_server->on("/",     HTTP_GET,  handleIndex);
  s_server->on("/join", HTTP_POST, handleJoin);
  s_server->onNotFound(handleCaptive);
  s_server->begin();

  // Block until joined. The unit already lit its resting indicator in uiInit() (which runs before
  // appBoot); leaving it be keeps this generic — the visible "sonos-button-setup" network is the
  // real signal that the box is waiting to be provisioned.
  while (!wifiIsConnected()) {
    s_dns->processNextRequest();
    s_server->handleClient();
    delay(2);
  }

  // Joined. Tear the portal down and return to plain STA so the rest of appBoot() (and the
  // :8080 config server, which waits on WL_CONNECTED) proceeds normally.
  s_server->stop();  delete s_server; s_server = nullptr;
  s_dns->stop();     delete s_dns;    s_dns    = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  Serial.println("[portal ] provisioned — continuing boot");
}

#endif  // HEADLESS
