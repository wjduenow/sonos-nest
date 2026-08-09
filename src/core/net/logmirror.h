// Mirror the serial log to a TCP socket, so a wall-mounted unit can be read without
// a cable.  `LOG` is a drop-in for `Serial`: it writes to the UART exactly as before and
// additionally to any connected client.
//
// OPT-IN PER ENV.  Without -DLOG_MIRROR this header collapses to `#define LOG Serial`
// and logmirror.cpp compiles to an empty translation unit, so units where internal SRAM
// is the binding constraint (nest, sleep-machine) pay nothing at all.  Only the jukebox
// enables it — it has ~70-100 KB of internal heap free where the S3 units have ~36 KB.
//
// !!! IT NEVER BLOCKS THE CALLER !!!  Writes are copied into a ring buffer and a
// dedicated task drains it to the socket.  A TCP write can stall for seconds when the
// far end stops reading, and this project has a long history of that class of bug:
// blocking the UI task freezes the panel (see the health-heartbeat note in
// plans/07-sonos-jukebox.md, where the diagnostic was inducing the freeze it exists to
// report).  Overflow drops the OLDEST bytes and is counted, so a slow reader degrades
// into gaps rather than into a hang.
#pragma once

#include <Arduino.h>

#ifdef LOG_MIRROR

class LogTee : public Print {
 public:
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buf, size_t len) override;
  int availableForWrite() override { return 512; }
};

extern LogTee LOG;

// Starts the listener task.  Safe to call before WiFi is up — the task waits for the
// link itself, the same way the sleep-machine's media httpd does, because appBoot()
// connects later than boardInit()/uiInit() run.
void logMirrorBegin(uint16_t port = 2323);

int      logMirrorClients();   // currently attached readers
uint32_t logMirrorDropped();   // bytes lost to overflow since boot
uint16_t logMirrorPort();

#else  // ---------------------------------------------------------------- disabled

#define LOG Serial
inline void     logMirrorBegin(uint16_t = 2323) {}
inline int      logMirrorClients() { return 0; }
inline uint32_t logMirrorDropped() { return 0; }
inline uint16_t logMirrorPort() { return 0; }

#endif
