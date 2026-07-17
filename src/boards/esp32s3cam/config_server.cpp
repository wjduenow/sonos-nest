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
  <div class=hint>The name your router lists this device under, so you can find its IP.
    Letters, digits and hyphens; spaces become hyphens. Saving reconnects Wi-Fi, so this page
    may hiccup for a few seconds.<br>
    This does <b>not</b> change the mDNS/OTA name, which stays <code id=mdns></code>.</div>
</div>

<div class=card>
  <div class=row>
    <label>Button</label>
    <span id=state style="opacity:.7">press it to start / stop</span>
  </div>
  <div class=hint>Press once: play the playlist above, looped. Press again: stop.</div>
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
