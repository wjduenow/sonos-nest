// A sonos-button's only configuration surface: a small web page on :8080.
//
// SHARED BY BOTH BUTTON BOARDS (esp32s3cam and xiao_esp32s3) — see this directory's README.
// It carries the whole embedded config page, so a second copy would guarantee the two units'
// pages silently drift apart. Nothing in it is board-specific: it includes core/webconfig.h,
// WiFi.h, WebServer.h and core/net/logmirror.h, and touches no pin and no board macro.
//
// These boxes have no screen, so the browser IS the settings UI. Shape lifted from
// boards/es3c28p/local_stream.cpp with the whole media/upload half dropped — there's no SD card
// here, so one port, and therefore no cross-origin/preflight problem to work around.
//
// Sockets and routing only. What is configurable and what a change MEANS lives in
// core/webconfig.* — a board must not reach into settings/g_pending itself.
#pragma once

void configServerStart();   // idempotent; its task waits for WiFi itself
