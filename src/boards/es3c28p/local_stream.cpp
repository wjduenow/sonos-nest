// ES3C28P HTTP media server — implements the board HAL's localFileUrl (core/board.h). Serves
// a file off the microSD over HTTP so the Nursery Sonos can stream it ("Play from Local",
// option 2). A tiny WebServer runs on its own task; localFileUrl() lazily starts it and
// returns the URL to hand Sonos.
//
// The route is a clean fixed path (/ocean.mp3) mapped to the real on-card filename, so the
// URL we give Sonos has no spaces/escaping to trip over.
#include "core/board.h"
#include "sd_card.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "SD_MMC.h"

static const uint16_t STREAM_PORT  = 8080;
static const char    *STREAM_ROUTE = "/ocean.mp3";

static WebServer   *s_server = nullptr;
static TaskHandle_t s_task   = nullptr;
static String       s_url;
static String       s_servePath;   // real SD path behind STREAM_ROUTE

static void handleMedia() {
  File f = SD_MMC.open(s_servePath.c_str(), FILE_READ);
  if (!f) { s_server->send(404, "text/plain", "not found"); return; }
  s_server->streamFile(f, "audio/mpeg");   // sends 200 + Content-Length + the bytes
  f.close();
}

static void serverTask(void *) {
  s_server->begin();
  for (;;) {
    s_server->handleClient();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

const char *localFileUrl(const char *path) {
  if (!sdEnsureMounted()) return nullptr;
  if (WiFi.status() != WL_CONNECTED) return nullptr;

  s_servePath = path;
  if (!s_server) {
    s_server = new WebServer(STREAM_PORT);
    s_server->on(STREAM_ROUTE, handleMedia);
    xTaskCreatePinnedToCore(serverTask, "media-httpd", 4096, nullptr, 1, &s_task, 0);
  }
  s_url = "http://" + WiFi.localIP().toString() + ":" + String(STREAM_PORT) + STREAM_ROUTE;
  return s_url.c_str();
}
