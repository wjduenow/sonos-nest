// The nest's web config surface: a small page on :8080, so its settings (Sonos room, screen
// brightness, device name) can be changed from a browser and the portal's "Open config" link
// works. The knob's on-device Settings screen still does the same things.
//
// Sockets and routing only — what is configurable and what a change MEANS lives in
// core/webconfig.* (a board must not reach into settings/g_pending itself). Shape lifted from
// boards/esp32s3cam/config_server.cpp.
#pragma once

void configServerStart();   // idempotent; its task waits for WiFi itself
