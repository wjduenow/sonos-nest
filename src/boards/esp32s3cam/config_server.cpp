// See config_server.h. Routing + sockets; meaning lives in core/webconfig.*.
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

// POST /api/config?field=<room|ring|...>&value=<...>
static void handleConfigSet() {
  String field = s_server->arg("field");
  String value = s_server->arg("value");
  String err;
  if (!webConfigApply(field, value, err)) { sendError(400, err.c_str()); return; }
  s_server->send(200, "application/json", webConfigJson());   // echo the new state back
}

static const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Sonos Button</title>
<style>
 :root{color-scheme:light dark}
 body{font:16px/1.5 system-ui,sans-serif;margin:0;padding:24px;max-width:26rem;margin-inline:auto}
 h1{font-size:1.25rem;margin:0 0 1.5rem}
 .card{border:1px solid #8883;border-radius:12px;padding:16px;margin-bottom:16px}
 .row{display:flex;align-items:center;justify-content:space-between;gap:12px}
 label{font-weight:600}
 button{font:inherit;padding:.5rem 1.1rem;border-radius:8px;border:1px solid #8886;
        background:#8881;cursor:pointer}
 button.on{background:#2a7;border-color:#2a7;color:#fff}
 input[type=range]{width:100%}
 select{font:inherit;padding:.4rem;border-radius:8px;max-width:12rem}
 #msg{min-height:1.5em;font-size:.85rem;opacity:.75}
 .hint{font-size:.8rem;opacity:.7;margin-top:.4rem}
</style>
<h1>Sonos Button</h1>

<div class=card>
  <div class=row>
    <label for=ringbtn>Button ring</label>
    <button id=ringbtn>…</button>
  </div>
  <input type=range id=ring min=0 max=100 step=5>
  <div class=hint>0 turns it fully off. The ring is on the underside of a nightstand — dim is
    usually plenty.</div>
</div>

<div class=card>
  <div class=row>
    <label for=room>Sonos room</label>
    <select id=room></select>
  </div>
  <div class=row style="margin-top:12px">
    <label for=playlist>Playlist</label>
    <select id=playlist></select>
  </div>
  <div class=hint id=plhint></div>
  <div class=row style="margin-top:12px">
    <label for=vol>Volume</label>
    <span id=volval></span>
  </div>
  <input type=range id=vol min=0 max=100 step=1>
  <div class=hint>The room is set to this volume each time the button starts the playlist.</div>
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
    <label>Button</label>
    <span id=state style="opacity:.7">press it to start / stop</span>
  </div>
  <div class=hint>Press once: play the playlist above, looped. Press again: stop.</div>
</div>

<div class=card>
  <div class=row>
    <label>Diagnostics</label>
    <span id=health style="opacity:.7;font-size:.8rem">…</span>
  </div>
  <div class=hint>Uptime, free memory and Sonos socket churn. If the button ever goes
    unresponsive, open this <b>before</b> power-cycling: free heap steadily falling points at a
    memory leak; reconnects climbing fast at socket/network churn.</div>
</div>

<div class=card>
  <div class=row>
    <label for=otaauto>Auto-update</label>
    <input type=checkbox id=otaauto>
  </div>
  <div class=row style="margin-top:12px">
    <label for=updmode>Update source</label>
    <select id=updmode>
      <option value=auto>Automatic</option>
      <option value=off>Off</option>
      <option value=custom>Custom URL…</option>
    </select>
  </div>
  <div class=row id=updurlrow hidden style="margin-top:8px">
    <input id=updurl autocapitalize=off autocorrect=off spellcheck=false placeholder="https://…/manifest.json" style="flex:1">
    <button id=usave>Save</button>
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

// The device is a single ESP32 serving one page — no need for anything but fetch.
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
  const on = st.ring>0;
  $('#ringbtn').textContent = on ? `On (${st.ring}%)` : 'Off';
  $('#ringbtn').classList.toggle('on', on);
  $('#ring').value = st.ring;

  fill($('#room'), (st.zones||[]).map(z=>z.name), st.room, '(no rooms found yet)');

  const pls = st.playlists||[];
  fill($('#playlist'), pls, st.playlist, '(loading playlists…)');
  // The list arrives from an async browse. Until it does, say so rather than letting the box
  // look like an empty/broken choice.
  $('#plhint').textContent = pls.length ? ''
    : `Still reading playlists from Sonos. Currently set to "${st.playlist}".`;

  $('#vol').value = st.volume;
  $('#volval').textContent = st.volume + '%';

  // Don't clobber what someone is mid-way through typing.
  if(document.activeElement !== $('#dname')) $('#dname').value = st.deviceName||'';
  $('#mdns').textContent = st.mdnsName||'';

  // Health readout (st.health from webConfigJson) — the numbers to capture when it misbehaves.
  const h=st.health||{};
  if(h.uptimeSec!=null){
    const up=h.uptimeSec, d=up/86400|0, hr=up%86400/3600|0, mn=up%3600/60|0;
    $('#health').innerHTML =
      `up ${d}d ${hr}h ${mn}m · heap ${(h.heapFree/1024|0)}K (min ${(h.heapMin/1024|0)}K) · `+
      `soap ${h.soapCalls||0} calls, <b>${h.soapReconnects||0}</b> reconnects, max ${h.soapMaxMs||0}ms`;
  }

  // Updates (core/net/updater). ota = {auto, updateUrl, source, sourceKind, running, available}.
  const o=st.ota||{};
  $('#otaauto').checked = !!o.auto;
  const raw=o.updateUrl||'';
  const mode = raw==='off' ? 'off' : (raw==='' ? 'auto' : 'custom');
  $('#updmode').value = mode;
  $('#updurlrow').hidden = (mode!=='custom');
  if(document.activeElement !== $('#updurl')) $('#updurl').value = (mode==='custom'?raw:'');
  const avail=o.available;
  $('#updrow').hidden = !(avail && !o.auto);
  if(avail) $('#updinfo').textContent = 'Ready: '+avail;
  const src = o.sourceKind==='portal' ? 'Portal'
    : o.sourceKind==='github' ? 'GitHub (latest release)'
    : o.sourceKind==='custom' ? (o.source||'custom') : 'Off';
  $('#otahint').innerHTML = 'Running <code>'+(o.running||'?')+'</code> · source: <b>'+src+'</b>. ' + (
    o.sourceKind==='off' ? 'Update checks are disabled.'
    : avail ? (o.auto ? 'Will auto-update to '+avail+' on next reboot.' : 'Version '+avail+' is ready to install.')
    : 'Up to date.');
}

// After changing the source, availability lands a moment later (the device re-checks on netTask);
// re-poll a few times so the "ready"/"up to date" line settles without a manual refresh.
function otaRecheck(){
  let n=0;
  const t=setInterval(async()=>{ st=await (await fetch('/api/config')).json(); draw(); if(++n>=3) clearInterval(t); },2000);
}

// Toggle remembers the last non-zero level, so Off->On restores your brightness rather than
// slamming to 100%.
let last=100;
$('#ringbtn').onclick=()=>{
  if(st.ring>0){ last=st.ring; post('ring',0); } else { post('ring',last||100); }
};
$('#ring').onchange=e=>post('ring',e.target.value);
$('#room').onchange=e=>post('room',e.target.value);
$('#playlist').onchange=e=>post('playlist',e.target.value);
$('#dsave').onclick=()=>post('deviceName',$('#dname').value);
$('#dname').onkeydown=e=>{ if(e.key==='Enter') post('deviceName',$('#dname').value) };
$('#otaauto').onchange=e=>post('otaAuto',e.target.checked?'1':'0');
$('#updmode').onchange=e=>{
  const m=e.target.value;
  if(m==='custom'){ $('#updurlrow').hidden=false; $('#updurl').focus(); }
  else post('updateUrl', m==='off'?'off':'').then(otaRecheck);   // auto -> "" ; off -> "off"
};
$('#usave').onclick=()=>post('updateUrl',$('#updurl').value).then(otaRecheck);
$('#updnow').onclick=async()=>{
  if(!confirm('Download and install '+((st.ota||{}).available||'the update')+'?\n\nThe device reboots to apply.')) return;
  await post('updateNow','1'); $('#msg').textContent='updating — the device will reboot…';
};
$('#vol').oninput=e=>$('#volval').textContent=e.target.value+'%';  // live while dragging
$('#vol').onchange=e=>post('volume',e.target.value);               // save on release only

// The playlist list lands a moment after boot; re-poll a few times so a page opened immediately
// after power-up fills in without a manual refresh.
let tries=0;
const poll=setInterval(async()=>{
  if((st.playlists||[]).length || ++tries>10){clearInterval(poll);return}
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
  // 4 KB: no SD_MMC stack sits on top of this one (that's why es3c28p's needs 8 KB).
  xTaskCreatePinnedToCore(serverTask, "cfg-httpd", 4096, nullptr, 1, &s_task, 0);
}
