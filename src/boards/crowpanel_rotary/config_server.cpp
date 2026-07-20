// See config_server.h. Routing + sockets; meaning lives in core/webconfig.*. Modeled on the
// sonos-button's config server, with the nest's applicable fields: room, brightness, device name.
#include "config_server.h"
#include "core/webconfig.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

static const uint16_t CONFIG_PORT = 8080;
static WebServer     *s_server = nullptr;
static TaskHandle_t   s_task   = nullptr;

static void sendError(int code, const char *msg) {
  s_server->send(code, "application/json", String("{\"error\":\"") + msg + "\"}");
}

// GET /api/config — current picks + available choices. Body built by core; we only carry it.
static void handleConfigGet() {
  s_server->send(200, "application/json", webConfigJson());
}

// POST /api/config?field=<room|brightness|deviceName>&value=<...>
static void handleConfigSet() {
  String field = s_server->arg("field");
  String value = s_server->arg("value");
  String err;
  if (!webConfigApply(field, value, err)) { sendError(400, err.c_str()); return; }
  s_server->send(200, "application/json", webConfigJson());   // echo the new state back
}

static const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Sonos Nest</title>
<style>
 :root{color-scheme:light dark}
 body{font:16px/1.5 system-ui,sans-serif;margin:0;padding:24px;max-width:26rem;margin-inline:auto}
 h1{font-size:1.25rem;margin:0 0 1.5rem}
 .card{border:1px solid #8883;border-radius:12px;padding:16px;margin-bottom:16px}
 .row{display:flex;align-items:center;justify-content:space-between;gap:12px}
 label{font-weight:600}
 button{font:inherit;padding:.5rem 1.1rem;border-radius:8px;border:1px solid #8886;
        background:#8881;cursor:pointer}
 input[type=range]{width:100%}
 select{font:inherit;padding:.4rem;border-radius:8px;max-width:12rem}
 #msg{min-height:1.5em;font-size:.85rem;opacity:.75}
 .hint{font-size:.8rem;opacity:.7;margin-top:.4rem}
</style>
<h1>Sonos Nest</h1>

<div class=card>
  <div class=row>
    <label for=room>Sonos room</label>
    <select id=room></select>
  </div>
  <div class=hint id=roomhint>The room this knob controls.</div>
</div>

<div class=card>
  <div class=row>
    <label for=bright>Brightness</label>
    <span id=brightval></span>
  </div>
  <input type=range id=bright min=10 max=100 step=5>
  <div class=hint>The round display's backlight. Floors at 10% so the screen can't go fully dark.</div>
</div>

<div class=card>
  <div class=row>
    <label for=dname>Device name</label>
    <span><input id=dname size=14 autocapitalize=off autocorrect=off spellcheck=false>
      <button id=dsave>Save</button></span>
  </div>
  <div class=hint>The name your router lists this device under, and the <code id=mdns></code>
    address (mDNS/OTA) both follow this. Letters, digits and hyphens; spaces become hyphens.
    <b>Saving restarts the device</b> to apply the new name — this page will drop; reopen it at
    the new name (or the same IP).</div>
</div>

<div class=card>
  <div class=row>
    <label for=otaauto>Auto-update</label>
    <input type=checkbox id=otaauto>
  </div>
  <div class=row style="margin-top:12px">
    <label for=updurl>Update source</label>
    <span><input id=updurl size=13 autocapitalize=off autocorrect=off spellcheck=false placeholder="(off)">
      <button id=usave>Save</button></span>
  </div>
  <div class=hint id=otahint></div>
  <div class=row id=updrow hidden style="margin-top:12px">
    <span id=updinfo></span>
    <button id=updnow>Update now</button>
  </div>
</div>

<div id=msg></div>

<script>
const $=s=>document.querySelector(s);
let st={};

async function post(field,value){
  $('#msg').textContent='saving…';
  const r=await fetch(`/api/config?field=${field}&value=${encodeURIComponent(value)}`,{method:'POST'});
  const j=await r.json();
  if(!r.ok){$('#msg').textContent='error: '+(j.error||r.status);return}
  st=j; draw(); $('#msg').textContent='saved';
}

function fill(sel,items,cur,empty){
  sel.innerHTML='';
  items.forEach(v=>{
    const o=document.createElement('option');
    o.value=o.textContent=v; o.selected=(v===cur); sel.append(o);
  });
  if(!items.length){const o=document.createElement('option');o.textContent=empty;sel.append(o)}
}

function draw(){
  const zones=(st.zones||[]).map(z=>z.name);
  fill($('#room'), zones, st.room, '(no rooms found yet)');
  $('#roomhint').textContent = zones.length ? 'The room this knob controls.'
    : 'Still discovering Sonos rooms…';

  $('#bright').value = st.brightness;
  $('#brightval').textContent = st.brightness + '%';

  // Don't clobber what someone is mid-way through typing.
  if(document.activeElement !== $('#dname')) $('#dname').value = st.deviceName||'';
  $('#mdns').textContent = st.mdnsName||'';

  // Updates (core/net/updater). ota = {auto, updateUrl, running, available}.
  const o=st.ota||{};
  $('#otaauto').checked = !!o.auto;
  if(document.activeElement !== $('#updurl')) $('#updurl').value = o.updateUrl||'';
  const avail=o.available;
  $('#updrow').hidden = !(avail && !o.auto);
  if(avail) $('#updinfo').textContent = 'Ready: '+avail;
  $('#otahint').innerHTML = 'Running <code>'+(o.running||'?')+'</code>. ' + (
    !o.updateUrl ? 'Set a manifest URL — your Sonos portal or a firmware release — to check for updates.'
    : avail ? (o.auto ? 'Will auto-update to '+avail+' on next reboot.' : 'Version '+avail+' is ready to install.')
    : 'Up to date.');
}

// After changing the source, availability lands a moment later (the device re-checks on netTask);
// re-poll a few times so the "ready"/"up to date" line settles without a manual refresh.
function otaRecheck(){
  let n=0;
  const t=setInterval(async()=>{ st=await (await fetch('/api/config')).json(); draw(); if(++n>=3) clearInterval(t); },2000);
}

$('#room').onchange=e=>post('room',e.target.value);
$('#bright').oninput=e=>$('#brightval').textContent=e.target.value+'%';  // live while dragging
$('#bright').onchange=e=>post('brightness',e.target.value);             // save on release only
$('#dsave').onclick=()=>post('deviceName',$('#dname').value);
$('#dname').onkeydown=e=>{ if(e.key==='Enter') post('deviceName',$('#dname').value) };
$('#otaauto').onchange=e=>post('otaAuto',e.target.checked?'1':'0');
$('#usave').onclick=()=>post('updateUrl',$('#updurl').value).then(otaRecheck);
$('#updnow').onclick=async()=>{
  if(!confirm('Download and install '+((st.ota||{}).available||'the update')+'?\n\nThe device reboots to apply.')) return;
  await post('updateNow','1'); $('#msg').textContent='updating — the device will reboot…';
};

// Rooms arrive from SSDP a moment after boot; re-poll a few times so a page opened immediately
// after power-up fills in without a manual refresh.
let tries=0;
const poll=setInterval(async()=>{
  if((st.zones||[]).length || ++tries>10){clearInterval(poll);return}
  st=await (await fetch('/api/config')).json(); draw();
},2000);

(async()=>{ st=await (await fetch('/api/config')).json(); draw(); })();
</script>)HTML";

static void handleIndex() { s_server->send_P(200, "text/html", kIndexHtml); }

static void serverTask(void *) {
  // boardInit() runs before appBoot() brings WiFi up, so wait for the link before binding.
  while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(250));

  s_server->begin();
  Serial.printf("\n[config ] ============================================\n");
  Serial.printf("[config ]   http://%s:%u/\n", WiFi.localIP().toString().c_str(), CONFIG_PORT);
  Serial.printf("[config ] ============================================\n\n");

  for (;;) {
    s_server->handleClient();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void configServerStart() {
  if (s_server) return;
  s_server = new WebServer(CONFIG_PORT);
  s_server->on("/",           HTTP_GET,  handleIndex);
  s_server->on("/api/config", HTTP_GET,  handleConfigGet);
  s_server->on("/api/config", HTTP_POST, handleConfigSet);
  xTaskCreatePinnedToCore(serverTask, "cfg-httpd", 4096, nullptr, 1, &s_task, 0);
}
