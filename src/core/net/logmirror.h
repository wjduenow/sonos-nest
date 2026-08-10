// Mirror the serial log to a TCP socket, so a wall-mounted unit can be read without
// a cable.  `LOG` is a drop-in for `Serial`: it writes to the UART exactly as before and
// additionally to any connected client.
//
// OPT-IN PER ENV.  Without -DLOG_MIRROR this header collapses to `#define LOG Serial`
// and logmirror.cpp compiles to an empty translation unit, so a unit that does not enable
// it pays nothing at all — not a byte of flash, not a task, not a socket.
//
// WHO HAS IT, AND WHY.  Measured free / minimum-ever internal heap on live devices:
//
//   sonos-jukebox   98 KB / 74 KB   YES — wall-mounted, no cable reachable. Its rear port is
//                                   power-only, so this and OTA are the only ways in.
//   sleep-button   243 KB / 226 KB  YES — HEADLESS. No screen at all, so without a cable there
//                                   is no way to observe it whatsoever, and it has the most
//                                   headroom of any unit by a wide margin.
//   sonos-nest      78 KB / 60 KB   no — has a screen showing its own state, and heapLargest is
//                                   still unknown on it (pre-ccfe157 firmware). Viable later;
//                                   read heapLargest from /api/config first.
//   sleep-machine   30 KB / 14.5 KB NO, and not a close call. 14.5 KB minimum is already in the
//                                   range CLAUDE.md documents as fatal: at ~15 KB free, LWIP
//                                   cannot get socket buffers and the SYMPTOM IS SONOS
//                                   "connection refused" — nothing that points at the cause. An
//                                   8 KB ring plus a listening socket is exactly the wrong thing
//                                   to add. It is also the unit with a screen AND a serial cable
//                                   within reach on a nightstand.
//
// ENABLING IT IS TWO THINGS, and only one of them is the flag: add -DLOG_MIRROR to the env AND
// call logMirrorBegin() from that unit's uiInit(). The flag alone compiles the module in but
// nothing ever starts the listener.
//
// It only tees `LOG`, never `Serial` — a file still calling Serial.print is invisible to a
// remote reader. core/ and the units that enable this use LOG throughout for that reason;
// bring-up/test sources deliberately keep Serial, since those run with a cable attached.
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
