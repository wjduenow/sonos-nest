// ES3C28P HTTP server — two jobs on one port (8080), one WebServer, one task:
//
//   1. Media streaming. Implements the board HAL's localFileUrl (core/board.h): serves a file
//      off the microSD so the Nursery Sonos can stream it ("Play from Local", option 2). The
//      route is a clean fixed path (/ocean.mp3) mapped to the real on-card filename, so the
//      URL we hand Sonos has no spaces/escaping to trip over.
//
//   2. Remote SD management. A small browser UI at http://<device-ip>:8080/ to list, upload,
//      and delete the sleep tracks on the card without pulling it out of the slot.
//
// The server task starts at boardInit() and waits for WiFi itself, so the management UI is up
// whether or not anything has streamed yet.
//
// Mutating routes (upload/delete) are refused with 409 while local audio is playing: the audio
// task streams MP3 off the same card, and writing to SD underneath it glitches playback.
#include "core/board.h"
#include "core/webconfig.h"   // what's remotely configurable + how to apply it (core owns that)
#include "local_stream.h"
#include "sd_card.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "SD_MMC.h"

static const uint16_t STREAM_PORT  = 8080;
static const char    *STREAM_ROUTE = "/ocean.mp3";

static WebServer   *s_server = nullptr;
static TaskHandle_t s_task   = nullptr;
static String       s_url;
static String       s_servePath;   // real SD path behind STREAM_ROUTE

// ---------------------------------------------------------------- helpers

// Accept only a bare .mp3 basename: no directories, no traversal. Everything lives in the
// card root, which is what localTracks* scans.
static bool safeName(const String &name) {
  if (name.isEmpty() || name.length() > 64) return false;
  if (name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 || name.indexOf("..") >= 0) return false;
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".mp3");
}

static void sendJson(int code, const JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  s_server->send(code, "application/json", out);
}

static void sendError(int code, const char *msg) {
  JsonDocument doc;
  doc["error"] = msg;
  sendJson(code, doc);
}

// ---------------------------------------------------------------- routes

// GET /ocean.mp3 — the fixed route Sonos streams from (see localFileUrl).
static void handleMedia() {
  File f = SD_MMC.open(s_servePath.c_str(), FILE_READ);
  if (!f) { s_server->send(404, "text/plain", "not found"); return; }
  s_server->streamFile(f, "audio/mpeg");   // sends 200 + Content-Length + the bytes
  f.close();
}

// GET /api/tracks — the card's .mp3 files plus capacity, for the management UI.
static void handleList() {
  if (!sdEnsureMounted()) { sendError(503, "no SD card"); return; }

  JsonDocument doc;
  doc["playing"] = localAudioActive();
  doc["total"]   = (uint64_t)SD_MMC.totalBytes();
  doc["used"]    = (uint64_t)SD_MMC.usedBytes();
  JsonArray arr  = doc["tracks"].to<JsonArray>();

  File root = SD_MMC.open("/");
  if (root) {
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      bool   dir  = f.isDirectory();
      String name = f.name();
      size_t size = f.size();
      f.close();
      if (dir) continue;
      int    slash = name.lastIndexOf('/');
      String base  = name.substring(slash + 1);
      String lower = base;
      lower.toLowerCase();
      if (!lower.endsWith(".mp3")) continue;
      JsonObject t = arr.add<JsonObject>();
      t["name"] = base;
      t["size"] = (uint64_t)size;
    }
    root.close();
  }
  sendJson(200, doc);
}

// --------------------------------------------------- upload (its own socket, port 8081)
//
// Uploads do NOT go through WebServer. Both of its body paths are unusable for an ~87 MB
// track on this core (framework-arduinoespressif32 @ 3.20017):
//
//   * multipart/form-data reads the body ONE BYTE AT A TIME (Parsing.cpp _uploadReadByte ->
//     client.read() per byte). Measured 109 KB/s on this board = 13 min for one sleep track.
//   * the raw-body path bulk-reads, but (a) it never calls _parseArguments(), so the query
//     string is invisible to the handler (arg("name") comes back empty and every upload is
//     refused), and (b) its read loop calls readBytes(buf, HTTP_RAW_BUFLEN), which blocks for
//     a FULL buffer — so any file whose length isn't an exact multiple of the buffer stalls
//     until the socket times out. That's essentially every real file.
//
// So we listen on our own socket and own the read loop: parse a minimal request line +
// Content-Length, then drain exactly that many bytes with non-blocking reads straight to SD.
// It's a separate port from the UI, hence the CORS header on the response.
static const uint16_t UPLOAD_PORT = 8081;
static WiFiServer    *s_uploadSrv = nullptr;
static uint8_t       *s_buf       = nullptr;   // socket -> SD bounce buffer (PSRAM)
static const size_t   BUF_SZ      = 16384;   // 64 KB measured identical — the card, not the
static const uint32_t IDLE_MS     = 10000;     // no bytes for this long => client gave up

static void uploadReply(WiFiClient &c, int code, const char *codeText, const char *json) {
  c.printf("HTTP/1.1 %d %s\r\n"
           "Content-Type: application/json\r\n"
           "Access-Control-Allow-Origin: *\r\n"   // the UI is served from :8080, we're :8081
           "Content-Length: %u\r\n"
           "Connection: close\r\n\r\n%s",
           code, codeText, (unsigned)strlen(json), json);
}

// Pull "name" out of "POST /upload?name=Foo%20Bar.mp3 HTTP/1.1".
static String queryName(const String &reqLine) {
  int q = reqLine.indexOf("?name=");
  if (q < 0) return "";
  int end = reqLine.indexOf(' ', q);
  String v = reqLine.substring(q + 6, end < 0 ? reqLine.length() : end);
  String out;                                    // percent-decode
  for (size_t i = 0; i < v.length(); i++) {
    if (v[i] == '%' && i + 2 < v.length()) {
      out += (char)strtol(v.substring(i + 1, i + 3).c_str(), nullptr, 16);
      i += 2;
    } else if (v[i] == '+') out += ' ';
    else                    out += v[i];
  }
  return out;
}

static void uploadServe(WiFiClient &c) {
  // Request line + headers. These are tiny, so line-at-a-time is fine here.
  String reqLine = c.readStringUntil('\n');
  size_t len = 0;
  for (;;) {
    String h = c.readStringUntil('\n');
    if (h.length() <= 1) break;                  // blank line => headers done
    h.trim();
    if (h.startsWith("Content-Length:") || h.startsWith("content-length:"))
      len = (size_t)h.substring(h.indexOf(':') + 1).toInt();
  }

  // CORS preflight. The page is served from :8080 and posts here to :8081, so the browser
  // treats it as cross-origin and OPTIONS-probes first whenever the request isn't "simple".
  // Answer it, or the upload dies before a single byte is sent.
  if (reqLine.startsWith("OPTIONS")) {
    c.print("HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Access-Control-Max-Age: 86400\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n");
    return;
  }

  String name = queryName(reqLine);
  if (localAudioActive()) { uploadReply(c, 409, "Conflict",  "{\"error\":\"stop playback first\"}"); return; }
  if (!sdEnsureMounted()) { uploadReply(c, 503, "Unavailable","{\"error\":\"no SD card\"}");        return; }
  if (!safeName(name))    { uploadReply(c, 400, "Bad Request","{\"error\":\"bad name\"}");          return; }
  if (len == 0)           { uploadReply(c, 400, "Bad Request","{\"error\":\"empty body\"}");        return; }

  String path = "/" + name;
  if (SD_MMC.exists(path)) SD_MMC.remove(path);  // overwrite by name
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f)                 { uploadReply(c, 500, "Error",      "{\"error\":\"open failed\"}");       return; }
  Serial.printf("[httpd] upload -> %s (%u bytes)\n", path.c_str(), (unsigned)len);

  // Accumulate into the bounce buffer and write full blocks rather than handing SD_MMC the
  // ~1.5 KB a socket read returns. Don't expect much from tuning this: the card itself writes
  // at ~180 KB/s regardless of block size (16 KB and 64 KB measured identical), and that — not
  // the network or this loop — is what sets upload speed. A few-minute track is ~5 MB / ~50 s.
  size_t   got  = 0;                             // bytes taken off the socket
  size_t   held = 0;                             // bytes in s_buf not yet written to SD
  uint32_t last = millis();
  bool     ok   = true;
  while (got < len && c.connected()) {
    size_t want = len - got;                     // never read past this request's body
    if (want > BUF_SZ - held) want = BUF_SZ - held;
    int n = c.read(s_buf + held, want);          // what's buffered right now; -1/0 if nothing yet
    if (n > 0) {
      held += n;
      got  += n;
      last  = millis();
      if (held == BUF_SZ) {                      // full block -> flush
        if (f.write(s_buf, held) != held) { Serial.println("[httpd] SD write failed (card full?)"); ok = false; break; }
        held = 0;
      }
    } else {
      if (millis() - last > IDLE_MS) { Serial.println("[httpd] upload stalled"); ok = false; break; }
      vTaskDelay(pdMS_TO_TICKS(2));              // nothing queued yet — yield, don't spin
    }
  }
  if (ok && held && f.write(s_buf, held) != held) {   // final short block
    Serial.println("[httpd] SD write failed (card full?)");
    ok = false;
  }
  f.close();

  if (!ok || got != len) {
    SD_MMC.remove(path);                         // don't leave a truncated track on the card
    uploadReply(c, 500, "Error", "{\"error\":\"upload incomplete\"}");
    return;
  }
  localTracksRefresh();                          // the on-device picker sees it on its next scan
  uploadReply(c, 200, "OK", "{\"ok\":true}");
  Serial.printf("[httpd] upload done: %s\n", path.c_str());
}

// Called from the server task loop; non-blocking.
static void uploadPoll() {
  if (!s_uploadSrv) return;
  WiFiClient c = s_uploadSrv->available();
  if (!c) return;
  uploadServe(c);
  c.flush();
  c.stop();
}

// GET /api/config — current picks + the choices available. The body is built by core; we only
// carry it. Same for the setter below: routing here, meaning there.
static void handleConfigGet() {
  s_server->send(200, "application/json", webConfigJson());
}

// POST /api/config?field=<sleepTrack|wakeTrack|room>&value=<...>
static void handleConfigSet() {
  String field = s_server->arg("field");
  String value = s_server->arg("value");
  String err;
  if (!webConfigApply(field, value, err)) { sendError(400, err.c_str()); return; }
  s_server->send(200, "application/json", webConfigJson());   // echo back the new state
}

// POST /api/delete?name=<basename>
static void handleDelete() {
  if (localAudioActive())        { sendError(409, "stop playback first"); return; }
  if (!sdEnsureMounted())        { sendError(503, "no SD card"); return; }
  String name = s_server->arg("name");
  if (!safeName(name))           { sendError(400, "bad name"); return; }

  String path = "/" + name;
  if (!SD_MMC.exists(path))      { sendError(404, "not found"); return; }
  if (!SD_MMC.remove(path))      { sendError(500, "delete failed"); return; }
  localTracksRefresh();
  webConfigTrackDeleted(path);   // don't leave the sleep/wake pick aimed at a file that's gone
  Serial.printf("[httpd] deleted %s\n", path.c_str());

  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
}

// GET / — the management UI. Embedded rather than served off SD so it still loads when the
// card is missing or empty (which is exactly when you need it).
static const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Sleep Machine — SD Card</title>
<style>
 body{font:16px/1.5 system-ui,sans-serif;margin:0;padding:24px;background:#12121a;color:#e8e8f0}
 h1{font-size:20px;font-weight:600;margin:0 0 4px}
 .sub{color:#8a8aa0;font-size:13px;margin-bottom:24px}
 .card{background:#1c1c28;border:1px solid #2a2a3a;border-radius:10px;padding:16px;margin-bottom:16px}
 .row{display:flex;align-items:center;justify-content:space-between;padding:10px 0;border-bottom:1px solid #2a2a3a}
 .row:last-child{border-bottom:0}
 .name{font-weight:500}.size{color:#8a8aa0;font-size:13px}
 button{background:#2a2a3a;color:#e8e8f0;border:0;border-radius:6px;padding:7px 14px;font:inherit;font-size:14px;cursor:pointer}
 button:hover{background:#3a3a4e}
 button.del{background:transparent;color:#ff6b6b}button.del:hover{background:#3a2028}
 button:disabled{opacity:.4;cursor:not-allowed}
 #drop{border:2px dashed #3a3a4e;border-radius:10px;padding:32px;text-align:center;color:#8a8aa0;cursor:pointer}
 #drop.over{border-color:#6c6cff;color:#e8e8f0}
 progress{width:100%;height:6px;margin-top:12px}
 .warn{color:#ffb86b;font-size:13px;margin-top:12px}
 h2{font-size:13px;font-weight:600;color:#8a8aa0;text-transform:uppercase;letter-spacing:.05em;margin:0 0 12px}
 .cfg{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:8px 0}
 .cfg label{font-weight:500}
 select{background:#12121a;color:#e8e8f0;border:1px solid #3a3a4e;border-radius:6px;padding:7px 10px;font:inherit;font-size:14px;min-width:55%}
 select:disabled{opacity:.4}
 input[type=text]{background:#12121a;color:#e8e8f0;border:1px solid #3a3a4e;border-radius:6px;padding:7px 10px;font:inherit;font-size:14px;width:9rem}
 .info{color:#8a8aa0;font-size:13px;padding:8px 0}
</style>
<h1>Sleep Machine</h1>
<div class=sub id=cap>loading…</div>
<div class=card>
  <h2>Settings</h2>
  <div class=cfg><label for=sleepTrack>Sleep track</label><select id=sleepTrack></select></div>
  <div class=cfg><label for=wakeTrack>Wake track</label><select id=wakeTrack></select></div>
  <div class=cfg><label for=room>Sonos room</label><select id=room></select></div>
</div>
<div class=card>
  <h2>Software update</h2>
  <div class=cfg><label for=otaauto>Auto-update</label><input type=checkbox id=otaauto></div>
  <div class=cfg><label for=updmode>Update source</label>
    <select id=updmode><option value=auto>Automatic</option><option value=off>Off</option><option value=custom>Custom URL…</option></select></div>
  <div class=cfg id=updurlrow hidden><input type=text id=updurl placeholder="https://…/manifest.json" style="flex:1"> <button id=usave>Save</button></div>
  <div class=info id=otaline></div>
  <div class=cfg id=updrow hidden><span id=updinfo></span><button id=updnow>Update now</button></div>
</div>
<div class=card id=list></div>
<div class=card>
  <div id=drop>Drop an .mp3 here, or click to choose</div>
  <input type=file id=file accept=.mp3 hidden>
  <progress id=prog value=0 max=100 hidden></progress>
  <div class=warn id=warn hidden>Playback is running — stop it on the device to upload or delete.</div>
</div>
<script>
const $=s=>document.querySelector(s), mb=b=>(b/1048576).toFixed(1)+' MB';
let playing=false;

// --- settings (sleep track / wake track / Sonos room) ---
function fill(sel,items,cur,emptyLabel,autoLabel){
  sel.innerHTML='';
  if(!items.length){
    sel.append(new Option(emptyLabel,''));
    sel.disabled=true;
    return;
  }
  sel.disabled=false;
  if(autoLabel) sel.append(new Option(autoLabel,''));   // "" = no explicit pick
  for(const it of items){
    const o=new Option(it.label,it.value);
    if(it.value===cur) o.selected=true;
    sel.append(o);
  }
}
async function loadCfg(){
  const r=await fetch('/api/config');
  if(!r.ok)return;
  const c=await r.json();
  const tracks=c.tracks.map(t=>({label:t.name,value:t.path}));
  fill($('#sleepTrack'),tracks,c.sleepTrack,'No tracks on the card');
  // wake has an Auto option: with no pick the device uses any file named "wake".
  fill($('#wakeTrack'),tracks,c.wakeTrack,'No tracks on the card','Auto (a file named "wake")');
  fill($('#room'),c.zones.map(z=>({label:z.name,value:z.name})),c.room,'No Sonos rooms found');

  // Software update (core/net/updater). ota = {auto, updateUrl, source, sourceKind, running, available}.
  const o=c.ota||{};
  $('#otaauto').checked=!!o.auto;
  const raw=o.updateUrl||'';
  const mode=raw==='off'?'off':(raw===''?'auto':'custom');
  $('#updmode').value=mode;
  $('#updurlrow').hidden=(mode!=='custom');
  if(document.activeElement!==$('#updurl')) $('#updurl').value=(mode==='custom'?raw:'');
  const avail=o.available;
  $('#updrow').hidden=!(avail&&!o.auto);
  if(avail)$('#updinfo').textContent='Ready: '+avail;
  const src=o.sourceKind==='portal'?'Portal':o.sourceKind==='github'?'GitHub (latest release)':o.sourceKind==='custom'?(o.source||'custom'):'Off';
  $('#otaline').innerHTML='Running <code>'+(o.running||'?')+'</code> · source: <b>'+src+'</b>. '+(
    o.sourceKind==='off'?'Update checks are disabled.'
    :avail?(o.auto?'Will auto-update to '+avail+' on next reboot.':'Version '+avail+' is ready to install.')
    :'Up to date.');
}
async function saveCfg(field,value){
  const q=`/api/config?field=${field}&value=${encodeURIComponent(value)}`;
  const r=await fetch(q,{method:'POST'});
  if(!r.ok){ alert((await r.json()).error||'could not save'); }
  loadCfg();   // re-read: the device is the source of truth
}
for(const f of ['sleepTrack','wakeTrack','room'])
  $('#'+f).onchange=e=>saveCfg(f,e.target.value);
// After changing the source, availability lands a moment later (device re-checks on netTask).
function otaRecheck(){let n=0;const t=setInterval(()=>{loadCfg();if(++n>=3)clearInterval(t);},2000);}
$('#otaauto').onchange=e=>saveCfg('otaAuto',e.target.checked?'1':'0');
$('#updmode').onchange=e=>{const m=e.target.value; if(m==='custom'){$('#updurlrow').hidden=false;$('#updurl').focus();} else {saveCfg('updateUrl',m==='off'?'off':'');otaRecheck();}};
$('#usave').onclick=()=>{saveCfg('updateUrl',$('#updurl').value);otaRecheck();};
$('#updnow').onclick=async()=>{
  if(!confirm('Download and install the update? The device reboots to apply.\n\nStop playback first if a sound is playing.'))return;
  await saveCfg('updateNow','1');
};

async function load(){
  const r=await fetch('/api/tracks');
  if(!r.ok){$('#cap').textContent='SD card not readable';return}
  const d=await r.json();
  playing=d.playing;
  $('#cap').textContent=`${d.tracks.length} track(s) · ${mb(d.used)} of ${mb(d.total)} used`;
  $('#warn').hidden=!playing;
  $('#list').innerHTML=d.tracks.length?'':'<div class=size>No tracks on the card.</div>';
  for(const t of d.tracks){
    const row=document.createElement('div');row.className='row';
    row.innerHTML=`<div><div class=name></div><div class=size>${mb(t.size)}</div></div>`;
    row.querySelector('.name').textContent=t.name;
    const b=document.createElement('button');b.className='del';b.textContent='Delete';b.disabled=playing;
    b.onclick=async()=>{
      if(!confirm(`Delete ${t.name}?`))return;
      const res=await fetch('/api/delete?name='+encodeURIComponent(t.name),{method:'POST'});
      if(!res.ok)alert((await res.json()).error||'delete failed');
      load();
    };
    row.appendChild(b);$('#list').appendChild(row);
  }
  loadCfg();   // the track list drives the dropdowns, so refresh them together
}
function upload(f){
  if(!f)return;
  if(playing){alert('Stop playback on the device first.');return}
  if(!f.name.toLowerCase().endsWith('.mp3')){alert('Only .mp3 files.');return}
  // Uploads go to the dedicated socket on :8081, not this WebServer — see local_stream.cpp.
  // Raw body, no FormData: the core's multipart parser reads it a byte at a time (~8x slower).
  const x=new XMLHttpRequest();
  x.open('POST',`http://${location.hostname}:8081/upload?name=`+encodeURIComponent(f.name));
  // text/plain is CORS-safelisted, so this stays a "simple" request and skips the preflight.
  // Sending the File as-is would set Content-Type: audio/mpeg and trigger one. The device
  // answers OPTIONS anyway, but not round-tripping it at all is faster and less fragile.
  x.setRequestHeader('Content-Type','text/plain');
  $('#prog').hidden=false;
  x.upload.onprogress=e=>{if(e.lengthComputable)$('#prog').value=e.loaded/e.total*100};
  x.onload=()=>{
    $('#prog').hidden=true;$('#prog').value=0;
    if(x.status!==200){try{alert(JSON.parse(x.responseText).error)}catch(_){alert('upload failed')}}
    load();
  };
  x.onerror=()=>{$('#prog').hidden=true;alert('upload failed')};
  x.send(f);
}
$('#drop').onclick=()=>$('#file').click();
$('#file').onchange=e=>upload(e.target.files[0]);
$('#drop').ondragover=e=>{e.preventDefault();$('#drop').classList.add('over')};
$('#drop').ondragleave=()=>$('#drop').classList.remove('over');
$('#drop').ondrop=e=>{e.preventDefault();$('#drop').classList.remove('over');upload(e.dataTransfer.files[0])};
load();
</script>)HTML";

static void handleIndex() {
  s_server->send_P(200, "text/html", kIndexHtml);
}

// ---------------------------------------------------------------- server task

static void serverTask(void *) {
  // boardInit() runs before appBoot() brings WiFi up, so wait for the link before binding.
  while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(250));

  s_server->begin();
  s_uploadSrv = new WiFiServer(UPLOAD_PORT);
  s_uploadSrv->begin();
  s_uploadSrv->setNoDelay(true);
  Serial.printf("[httpd] http://%s:%u/  (uploads on :%u)\n",
                WiFi.localIP().toString().c_str(), STREAM_PORT, UPLOAD_PORT);

  for (;;) {
    s_server->handleClient();
    uploadPoll();                 // blocks this task for the duration of an upload — fine,
    vTaskDelay(pdMS_TO_TICKS(2)); // nothing else runs here and the UI is on the other core
  }
}

void mediaServerStart() {
  if (s_server) return;
  // Socket -> SD bounce buffer in PSRAM (internal SRAM is the tight resource on this board).
  s_buf = (uint8_t *)heap_caps_malloc(BUF_SZ, MALLOC_CAP_SPIRAM);
  if (!s_buf) s_buf = (uint8_t *)malloc(BUF_SZ);   // no PSRAM? fall back to the heap
  s_server = new WebServer(STREAM_PORT);
  s_server->on("/",            HTTP_GET,  handleIndex);
  s_server->on("/api/tracks",  HTTP_GET,  handleList);
  s_server->on("/api/config",  HTTP_GET,  handleConfigGet);
  s_server->on("/api/config",  HTTP_POST, handleConfigSet);
  s_server->on("/api/delete",  HTTP_POST, handleDelete);
  s_server->on(STREAM_ROUTE,   HTTP_GET,  handleMedia);
  // 8 KB, not the 4 KB the stream-only server used: the upload path writes to SD from inside
  // this task (SD_MMC's stack sits on top of ours).
  xTaskCreatePinnedToCore(serverTask, "media-httpd", 8192, nullptr, 1, &s_task, 0);
}

// ---------------------------------------------------------------- board HAL

const char *localManagerUrl() {
  if (WiFi.status() != WL_CONNECTED) return nullptr;
  static String url;
  url = "http://" + WiFi.localIP().toString() + ":" + String(STREAM_PORT);
  return url.c_str();
}

// The sleep-machine's :8080 server IS the config UI (SD file management + Sonos room/track picks),
// so its config URL is the same as its file-manager URL.
const char *boardConfigUrl() { return localManagerUrl(); }

const char *localFileUrl(const char *path) {
  if (!sdEnsureMounted()) return nullptr;
  if (WiFi.status() != WL_CONNECTED) return nullptr;

  s_servePath = path;
  mediaServerStart();   // no-op if boardInit() already did it

  // The route is a fixed path, so every file would otherwise get the SAME URL — and Sonos keys
  // its cache (content AND metadata) off the URL. Swapping the sleep track for the wake track
  // behind an unchanged URL makes it replay the cached sleep track. Bump a counter into the
  // query string so each call is a distinct URL; WebServer matches on the path and ignores it.
  static uint32_t seq = 0;
  s_url = "http://" + WiFi.localIP().toString() + ":" + String(STREAM_PORT) + STREAM_ROUTE +
          "?v=" + String(++seq);
  return s_url.c_str();
}
