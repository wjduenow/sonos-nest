// Config web server for the CrowPanel Advance 7". Implements boardConfigUrl() from core/board.h.
//
// Deliberately much smaller than the sleep-machine's server (boards/es3c28p/local_stream.cpp),
// because this board has a different job: that one manages FILES on an SD card and needs upload
// handling, a second socket and CORS. This one only reads and writes settings, so it is one
// WebServer on port 80 with three routes and an embedded page.
//
// LAYERING, per CLAUDE.md: this does sockets and routing and nothing else. What is configurable,
// where it persists, and how a change is applied all live in core/webconfig — a board's HTTP
// server must not reach into settings or g_pending itself. The Radio and Favorites schedule fields
// it exposes are therefore the same ones the on-screen Settings page writes, through the same code
// path — which is also why adding the Favorites schedule needed a core/webconfig change and not
// just markup here.
#include <Arduino.h>
#include "core/net/logmirror.h"
#include <WebServer.h>
#include <WiFi.h>

#include "core/board.h"
#include "core/fav_cache.h"
#include "core/radio_cache.h"
#include "core/webconfig.h"
#include "knob.h"
#include "web_config.h"

static WebServer *s_server = nullptr;
static String     s_url;

// A flash literal rather than a String built per request: assembling a few KB of markup on a board
// whose internal SRAM is the scarce resource is not worth the convenience.
static const char kPage[] = R"HTML(<!doctype html>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sonos Jukebox</title>
<style>
:root{--bg:#0e0f12;--elev:#191b20;--elev2:#23262d;--line:#2c3038;--text:#f4f5f7;
      --mut:#9aa0ab;--dim:#6a7079;--accent:#e8892b}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.5 system-ui,sans-serif;
     padding:24px;max-width:640px;margin:0 auto}
h1{font-size:22px;margin:0 0 4px}
p.sub{color:var(--dim);margin:0 0 28px;font-size:14px}
section{background:var(--elev);border:1px solid var(--line);border-radius:14px;
        padding:18px;margin-bottom:16px}
h2{font-size:15px;margin:0 0 14px;color:var(--mut);font-weight:600;
   text-transform:uppercase;letter-spacing:.08em}
label{display:block;margin:14px 0 6px;font-size:14px;color:var(--mut)}
select,input[type=text],input[type=number],input[type=range]{width:100%;padding:11px 12px;border-radius:10px;
  border:1px solid var(--line);background:var(--elev2);color:var(--text);font-size:16px}
.row{display:flex;gap:12px;align-items:center}
.row>*{flex:1}
button{background:var(--accent);color:#fff;border:0;border-radius:10px;padding:12px 18px;
       font-size:15px;font-weight:600;cursor:pointer}
button.ghost{background:var(--elev2);color:var(--text)}
.sw{display:flex;align-items:center;justify-content:space-between;gap:12px}
.sw input{width:auto;flex:0}
#msg{margin-top:14px;font-size:14px;color:var(--dim);min-height:20px}
</style>
<h1>Sonos Jukebox</h1>
<p class="sub" id="ident">&nbsp;</p>

<section>
  <h2>Room</h2>
  <select id="room"></select>
</section>

<section>
  <h2>Radio stations</h2>
  <div class="sw"><label for="auto" style="margin:0">Refresh the station list daily</label>
    <input type="checkbox" id="auto"></div>
  <label for="hour">At (device local time)</label>
  <div class="row">
    <select id="hour"></select>
    <button class="ghost" id="now" type="button">Refresh now</button>
  </div>
</section>

<section>
  <h2>Sound</h2>
  <label for="snd">Feedback level <span id="sndv" style="color:var(--text)"></span></label>
  <input type="range" id="snd" min="0" max="100" step="5">
  <p style="color:var(--dim);font-size:13px;margin:6px 0 0">0 turns all feedback off.</p>
  <div class="sw" style="margin-top:14px">
    <label for="scroll" style="margin:0">Clicks while scrolling</label>
    <input type="checkbox" id="scroll"></div>
</section>

<section>
  <h2>Favorites</h2>
  <div class="sw"><label for="favauto" style="margin:0">Refresh the favorites list daily</label>
    <input type="checkbox" id="favauto"></div>
  <label for="favhour">At (device local time)</label>
  <div class="row">
    <select id="favhour"></select>
    <button class="ghost" id="favnow" type="button">Refresh now</button>
  </div>
  <p style="color:var(--dim);font-size:13px;margin:10px 0 0">
    Pick a different hour from the station refresh — running both at once is more memory than
    the device has to spare.</p>
</section>

<section>
  <h2>Screensaver</h2>
  <label for="ssmode">Show when idle</label>
  <select id="ssmode">
    <option value="3">Album art when playing, clock otherwise</option>
    <option value="2">Album art</option>
    <option value="1">Clock</option>
    <option value="0">Nothing</option>
  </select>
  <label for="ssdelay">Appears after</label>
  <select id="ssdelay"></select>
  <label for="ssdim">Brightness while it is up <span id="ssdimv" style="color:var(--text)"></span></label>
  <input type="range" id="ssdim" min="5" max="100" step="5">
  <label for="ssblank">Turn the screen off after</label>
  <select id="ssblank"></select>
  <p style="color:var(--dim);font-size:13px;margin:10px 0 0">
    Touching the screen or turning the dial wakes it, even with the backlight off. This panel is an
    IPS LCD, so image retention is temporary — the screen-off timer is what actually prevents it.</p>
</section>

<section>
  <h2>Device</h2>
  <label for="name">Name (also the network hostname)</label>
  <div class="row"><input type="text" id="name" maxlength="31">
    <button class="ghost" id="savename" type="button" style="flex:0 0 auto">Save</button></div>
  <label for="bright">Screen brightness</label>
  <input type="number" id="bright" min="10" max="100">
</section>

<section>
  <h2>Software update</h2>
  <div class="sw"><label for="otaauto" style="margin:0">Install updates automatically</label>
    <input type="checkbox" id="otaauto"></div>
  <label for="updmode">Update source</label>
  <select id="updmode">
    <option value="auto">Automatic</option>
    <option value="off">Off</option>
    <option value="custom">Custom URL&hellip;</option>
  </select>
  <div class="row" id="updurlrow" hidden style="margin-top:10px">
    <input type="text" id="updurl" autocapitalize="off" autocorrect="off" spellcheck="false"
           placeholder="https://&hellip;/manifest.json">
    <button class="ghost" id="usave" type="button" style="flex:0 0 auto">Save</button></div>
  <p style="color:var(--dim);font-size:13px;margin:10px 0 0" id="otahint"></p>
  <div class="row" id="updrow" hidden style="margin-top:12px">
    <span id="updinfo" style="font-size:14px"></span>
    <button id="updnow" type="button" style="flex:0 0 auto">Update now</button></div>
</section>

<div id="msg"></div>
<script>
const $=s=>document.querySelector(s), msg=t=>$('#msg').textContent=t;
async function put(field,value){
  const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'text/plain'},
    body:JSON.stringify({field,value:String(value)})});
  const j=await r.json().catch(()=>({}));
  // A save can succeed AND carry a note (e.g. both refreshes scheduled in the same hour).
  msg(r.ok&&j.ok!==false ? ('Saved.'+(j.note?' '+j.note:''))
                         : ('Could not save: '+(j.error||r.status)));
}
async function load(){
  const c=await (await fetch('/api/config')).json();
  $('#ident').textContent=(c.deviceName||'jukebox')+' · '+(c.ip||'')+' · '+(c.fwVersion||'');
  const rs=$('#room'); rs.innerHTML='';
  (c.zones||[]).forEach(z=>{const o=document.createElement('option');
    o.value=o.textContent=z.name; if(z.name===c.room)o.selected=true; rs.append(o)});
  const fillHours=(sel,cur)=>{sel.innerHTML='';
    for(let h=0;h<24;h++){const o=document.createElement('option');
      o.value=h; o.textContent=String(h).padStart(2,'0')+':00';
      if(h===cur)o.selected=true; sel.append(o)}};
  fillHours($('#hour'), c.radio_refresh_hour);
  fillHours($('#favhour'), c.fav_refresh_hour);
  $('#auto').checked=!!c.radio_auto_refresh;
  $('#favauto').checked=!!c.fav_auto_refresh;
  $('#snd').value=c.uiSound??40; $('#sndv').textContent=($('#snd').value)+'%';
  $('#scroll').checked=!!c.scrollSound; $('#scroll').disabled=Number(c.uiSound)===0;
  $('#name').value=c.deviceName||''; $('#bright').value=c.brightness??100;
  // Screensaver. The two timers are presets rather than free numbers — nobody wants to type 1800,
  // and "Never" (0) has to be reachable without discovering that 0 is special.
  const fillOpts=(sel,opts,cur)=>{sel.innerHTML='';
    opts.forEach(([v,t])=>{const o=document.createElement('option');
      o.value=v; o.textContent=t; if(Number(v)===Number(cur))o.selected=true; sel.append(o)})};
  fillOpts($('#ssdelay'),[[0,'Never'],[30,'30 seconds'],[60,'1 minute'],[120,'2 minutes'],
    [300,'5 minutes'],[600,'10 minutes'],[1800,'30 minutes']], c.saver_delay_sec??120);
  fillOpts($('#ssblank'),[[0,'Never'],[5,'5 minutes'],[15,'15 minutes'],[30,'30 minutes'],
    [60,'1 hour'],[120,'2 hours'],[480,'8 hours']], c.saver_blank_min??60);
  $('#ssmode').value=String(c.saver_mode??3);
  $('#ssdim').value=c.saver_dim??40; $('#ssdimv').textContent=($('#ssdim').value)+'%';
  drawOta(c);
}
// Software update (core/net/updater). c.ota = {auto, updateUrl, source, sourceKind, running,
// available}. updateUrl is the RAW stored tri-state, which is what the mode select shows:
// "" = automatic, "off" = disabled, anything else = a custom manifest URL.
let otaAvail='';
function drawOta(c){
  const o=c.ota||{};
  $('#otaauto').checked=!!o.auto;
  const raw=o.updateUrl||'';
  const mode = raw==='off' ? 'off' : (raw==='' ? 'auto' : 'custom');
  $('#updmode').value=mode;
  $('#updurlrow').hidden=(mode!=='custom');
  if(document.activeElement!==$('#updurl')) $('#updurl').value=(mode==='custom'?raw:'');
  otaAvail=o.available||'';
  // "Update now" appears only when something is waiting AND auto is off — an auto device applies it
  // at its next boot by itself, so offering the button there just invites a needless reboot.
  $('#updrow').hidden=!(otaAvail&&!o.auto);
  if(otaAvail) $('#updinfo').textContent='Ready: '+otaAvail;
  const src = o.sourceKind==='portal' ? 'Portal'
    : o.sourceKind==='github' ? 'GitHub (latest release)'
    : o.sourceKind==='custom' ? (o.source||'custom') : 'Off';
  $('#otahint').innerHTML='Running <code>'+(o.running||'?')+'</code> &middot; source: <b>'+src+'</b>. '+(
    o.sourceKind==='off' ? 'Update checks are disabled.'
    : otaAvail ? (o.auto ? 'Will auto-update to '+otaAvail+' on next reboot.'
                         : 'Version '+otaAvail+' is ready to install.')
    : 'Up to date.');
}
// Changing the source only queues a re-check (updaterForceCheck runs on netTask), so availability
// lands a second or two later — re-poll a few times so the hint settles without a manual reload.
// Redraws ONLY the update widgets: a full load() would overwrite the device-name field under
// someone still typing in it.
function otaRecheck(){
  let n=0;
  const pull=async()=>drawOta(await (await fetch('/api/config')).json());
  pull();   // at once, so toggling auto hides/shows the button now rather than in two seconds
  const t=setInterval(()=>{pull(); if(++n>=3) clearInterval(t)},2000);
}
$('#snd').oninput=e=>$('#sndv').textContent=e.target.value+'%';
$('#snd').onchange=e=>{put('uiSound',e.target.value);
  $('#scroll').disabled=Number(e.target.value)===0};
$('#scroll').onchange=e=>put('scrollSound',e.target.checked?'1':'0');
$('#room').onchange=e=>put('room',e.target.value);
$('#hour').onchange=e=>put('radio_refresh_hour',e.target.value);
$('#auto').onchange=e=>put('radio_auto_refresh',e.target.checked?'1':'0');
$('#favhour').onchange=e=>put('fav_refresh_hour',e.target.value);
$('#favauto').onchange=e=>put('fav_auto_refresh',e.target.checked?'1':'0');
$('#bright').onchange=e=>put('brightness',e.target.value);
$('#ssmode').onchange=e=>put('saver_mode',e.target.value);
$('#ssdelay').onchange=e=>put('saver_delay_sec',e.target.value);
$('#ssblank').onchange=e=>put('saver_blank_min',e.target.value);
$('#ssdim').oninput=e=>$('#ssdimv').textContent=e.target.value+'%';
$('#ssdim').onchange=e=>put('saver_dim',e.target.value);
$('#otaauto').onchange=e=>put('otaAuto',e.target.checked?'1':'0').then(otaRecheck);
$('#updmode').onchange=e=>{
  const m=e.target.value;
  if(m==='custom'){$('#updurlrow').hidden=false;$('#updurl').focus()}
  else put('updateUrl', m==='off'?'off':'').then(otaRecheck);   // auto -> "" ; off -> "off"
};
$('#usave').onclick=()=>put('updateUrl',$('#updurl').value).then(otaRecheck);
$('#updnow').onclick=async()=>{
  if(!confirm('Download and install '+(otaAvail||'the update')+'?\n\nThe device reboots to apply.')) return;
  await put('updateNow','1');
  msg('Updating — the device reboots when the download finishes.');
};
$('#savename').onclick=()=>put('deviceName',$('#name').value)
  .then(()=>msg('Saved. The device reboots to register the new name.'));
$('#favnow').onclick=async()=>{msg('Refreshing favorites...');
  const r=await fetch('/api/favorites/refresh',{method:'POST'});
  msg(r.ok?'Favorites refresh started.':'Could not start a refresh.')};
$('#now').onclick=async()=>{msg('Refreshing...');
  const r=await fetch('/api/radio/refresh',{method:'POST'});
  msg(r.ok?'Refresh started. It takes about a minute.':'Could not start a refresh.')};
load();
</script>
)HTML";

// send(), not send_P(): PROGMEM is a no-op on the ESP32 (flash is directly addressable) and the
// _P variant returned an empty body here while the JSON routes worked fine.
static void handleRoot()   { s_server->send(200, "text/html", kPage); }
static void handleGet()    { s_server->send(200, "application/json", webConfigJson()); }

static void handlePost() {
  // Body is JSON but parsing it with ArduinoJson here would pull the whole document into internal
  // RAM for two short strings, so pick the two fields out directly.
  const String body = s_server->arg("plain");
  auto pick = [&](const char *k) -> String {
    const String tag = String("\"") + k + "\"";
    int i = body.indexOf(tag);
    if (i < 0) return "";
    i = body.indexOf(':', i + tag.length());
    if (i < 0) return "";
    while (i < (int)body.length() && (body[i] == ':' || body[i] == ' ')) i++;
    if (i >= (int)body.length() || body[i] != '"') return "";
    const int e = body.indexOf('"', ++i);
    return (e < 0) ? "" : body.substring(i, e);
  };
  // Accept either shape. WebServer only exposes the raw body as arg("plain") when the content type
  // is NOT form-encoded — so a client posting application/x-www-form-urlencoded (curl's default)
  // silently yields an empty body and a confusing "no field". Taking real args first makes
  //   curl -d 'field=radio_refresh_hour&value=6'
  // work identically to the JSON the page sends.
  const String field = s_server->hasArg("field") ? s_server->arg("field") : pick("field");
  const String value = s_server->hasArg("value") ? s_server->arg("value") : pick("value");
  String err;
  if (field.isEmpty()) { s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"no field\"}"); return; }
  if (!webConfigApply(field, value, err)) {
    s_server->send(400, "application/json", String("{\"ok\":false,\"error\":\"") + err + "\"}");
    return;
  }
  // webConfigApply may fill err on SUCCESS as a non-blocking note — a setting that is legal but
  // worth warning about, like scheduling both refreshes in the same hour. Pass it through rather
  // than dropping it; the page shows it beside "Saved.".
  if (err.length()) {
    String e = err; e.replace("\"", "'");
    s_server->send(200, "application/json", String("{\"ok\":true,\"note\":\"") + e + "\"}");
    return;
  }
  s_server->send(200, "application/json", "{\"ok\":true}");
}

static void handleRefresh() {
  radiocache::requestRefresh();
  s_server->send(200, "application/json", "{\"ok\":true}");
}

// Read-only; no side effects beyond re-probing the bus.
static void handleKnob() { s_server->send(200, "application/json", knobDiagJson()); }

static void handleFavRefresh() {
  favcache::requestRefresh();
  s_server->send(200, "application/json", "{\"ok\":true}");
}

static void serverTask(void *) {
  // boardInit() runs before appBoot() brings up WiFi, so wait here rather than failing to bind.
  while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(500));

  s_server = new WebServer(80);
  s_server->on("/", HTTP_GET, handleRoot);
  s_server->on("/api/config", HTTP_GET, handleGet);
  s_server->on("/api/config", HTTP_POST, handlePost);
  s_server->on("/api/radio/refresh", HTTP_POST, handleRefresh);
  s_server->on("/api/favorites/refresh", HTTP_POST, handleFavRefresh);
  s_server->on("/api/knob", HTTP_GET, handleKnob);
  s_server->begin();
  s_url = String("http://") + WiFi.localIP().toString();
  LOG.printf("[web   ] config server on %s\n", s_url.c_str());

  for (;;) {
    s_server->handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool webConfigServerInit() {
  // Core 0 with the rest of the networking, low priority: serving a settings page must never
  // compete with rendering, and it is idle almost always.
  return xTaskCreatePinnedToCore(serverTask, "webcfg", 8192, nullptr, 1, nullptr, 0) == pdPASS;
}

// --- core/board.h ---------------------------------------------------------------------------------
// nullptr until the server is actually listening, so the portal never advertises a dead link.
const char *boardConfigUrl() { return s_url.isEmpty() ? nullptr : s_url.c_str(); }
