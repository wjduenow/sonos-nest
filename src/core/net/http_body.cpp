// See http_body.h.
#include "http_body.h"

#include <WiFiClient.h>
#include <stdlib.h>

namespace httpbody {

void prepare(HTTPClient &http, const char *const *also, size_t nAlso) {
  const char *keys[6] = {"Transfer-Encoding"};
  size_t n = 1;
  for (size_t i = 0; i < nAlso && n < 6; ++i) keys[n++] = also[i];
  http.collectHeaders(keys, n);
}

static bool   expired(uint32_t deadline) { return (int32_t)(millis() - deadline) >= 0; }
static size_t minz(size_t a, size_t b)   { return a < b ? a : b; }

// Sleep (really sleep) until a byte is readable. False = the peer closed or the deadline passed.
// available() is checked first so bytes already buffered on a closed socket are still delivered.
static bool waitData(WiFiClient *c, uint32_t deadline) {
  while (!c->available()) {
    if (!c->connected()) return false;
    if (expired(deadline)) return false;
    delay(5);
  }
  return true;
}

// One CRLF-terminated line, CR stripped. Only ever used for chunk-size lines, hence the small cap.
static bool readLine(WiFiClient *c, char *line, size_t cap, uint32_t deadline) {
  size_t n = 0;
  for (;;) {
    if (!waitData(c, deadline)) return false;
    const int ch = c->read();
    if (ch < 0) continue;
    if (ch == '\n') { line[n] = 0; return true; }
    if (ch == '\r') continue;
    if (n + 1 >= cap) return false;
    line[n++] = (char)ch;
  }
}

// Exactly `want` bytes to the sink, or fewer if the sink stops. -1 = the bytes never came.
static long readN(WiFiClient *c, size_t want, uint32_t deadline, Sink sink, void *ctx, bool &stopped) {
  uint8_t buf[1024];
  size_t total = 0;
  while (total < want) {
    if (!waitData(c, deadline)) return -1;
    const size_t take = minz(minz((size_t)c->available(), want - total), sizeof buf);
    const int n = c->read(buf, take);
    if (n <= 0) continue;
    total += (size_t)n;
    if (!sink(ctx, buf, (size_t)n)) { stopped = true; break; }
  }
  return (long)total;
}

long read(HTTPClient &http, uint32_t maxMs, Sink sink, void *ctx) {
  WiFiClient *c = http.getStreamPtr();
  if (!c) return -1;
  const uint32_t deadline = millis() + maxMs;
  const bool chunked = http.header("Transfer-Encoding").indexOf("chunked") >= 0;
  const long len = http.getSize();          // -1 when the response carries no Content-Length
  bool stopped = false;

  if (chunked) {
    long total = 0;
    char line[32];
    for (;;) {
      if (!readLine(c, line, sizeof line, deadline)) return -1;
      if (!line[0]) continue;                          // a stray blank line; be lenient
      char        *end = nullptr;
      const size_t sz  = strtoul(line, &end, 16);      // stops at a ';' chunk extension by itself
      if (end == line) return -1;                      // no hex digits: framing is lost, not "done"
      if (sz == 0) return total;                       // the last chunk; trailers are not wanted
      const long n = readN(c, sz, deadline, sink, ctx, stopped);
      if (n < 0) return -1;
      total += n;
      if (stopped) return total;
      if (!readLine(c, line, sizeof line, deadline)) return -1;   // the CRLF after the data
    }
  }

  if (len >= 0) return readN(c, (size_t)len, deadline, sink, ctx, stopped);

  // No length and not chunked: the body ends when the server closes.
  uint8_t buf[1024];
  long total = 0;
  for (;;) {
    if (!waitData(c, deadline)) return expired(deadline) ? -1 : total;
    const int n = c->read(buf, minz((size_t)c->available(), sizeof buf));
    if (n <= 0) continue;
    total += n;
    if (!sink(ctx, buf, (size_t)n)) return total;
  }
}

}  // namespace httpbody
