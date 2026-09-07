// Read an HTTP response body without EVER sitting in a loop that starves IDLE0.
//
// HTTPClient::writeToStream() is the obvious way to read a body and it is the third thing in this
// tree to reboot a device: on a chunked response it reads every chunk header with
// readStringUntil() — Stream::timedRead(), a busy-wait with no yield — and separates chunks with
// delay(0), which yields only to tasks of EQUAL priority and so never lets IDLE0 run. A source that
// dribbles chunks (a speaker's /getaa proxying a station's cover from a CDN) therefore starves the
// watched idle task ACROSS chunks even though every single read is under the 5 s watchdog. Decoded
// from a coredump on 2026-09-07: task `art`, PC in xQueueGenericSend under lwIP's core lock, panic
// "IDLE0 (CPU 0) did not reset the watchdog". A per-read timeout under 5 s does not help, because
// it bounds one read, not the gap between them.
//
// This reads the body itself: chunked or Content-Length or read-to-close, blocks of up to 1 KB,
// and delay(5) — a real sleep — whenever the socket has nothing, under one overall deadline.
#pragma once

#include <Arduino.h>
#include <HTTPClient.h>

namespace httpbody {

// Call BEFORE http.GET(). HTTPClient discards headers it was not asked to keep, and the reader
// needs Transfer-Encoding to know how the body is framed. Pass any headers the caller also wants
// kept — collectHeaders() REPLACES the list, so two callers cannot each ask for their own.
void prepare(HTTPClient &http, const char *const *also = nullptr, size_t nAlso = 0);

// Receives body bytes. Return false to stop early (buffer full); the read ends there.
using Sink = bool (*)(void *ctx, const uint8_t *data, size_t n);

// Bytes delivered to the sink, or -1 when the body could not be read to its end: deadline,
// disconnect mid-body, or malformed chunk framing. Stopping the sink early is NOT an error —
// the caller knows it did that.
long read(HTTPClient &http, uint32_t maxMs, Sink sink, void *ctx);

}  // namespace httpbody
