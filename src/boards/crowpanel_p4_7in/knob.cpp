// See knob.h. Modulino Knob driver — implements encoderDelta()/knobEvent()/knobPressed()/knobDown()
// from core/board.h.
//
// PROTOCOL (derived from Arduino's own Modulino library, since the datasheet does not document the
// register map). There is no register/offset byte in either direction:
//
//   read : requestFrom(addr, 4) -> [0] pinstrap address   <-- NOT DATA. This is the trap.
//                                  [1] position, low byte
//                                  [2] position, high byte
//                                  [3] button, non-zero = pressed
//   write: raw bytes, no prefix (we never write — see below)
//
// The library asks for `howmany + 1` and throws the first byte away into its address-discovery
// field. Read 3 bytes instead of 4 and every field is shifted by one: the position's low byte
// becomes the pinstrap, the button lands in the high byte, and the dial appears to jump in
// thousands and never register a press. Plausible-looking garbage, not an error.
//
// WE NEVER CALL set(). The library's begin() writes a position back to the device to test for a
// firmware quirk where set() negates its argument (`_bug_on_set`). We do not need absolute
// positions at all — only deltas — so not writing sidesteps that quirk entirely and keeps the
// device read-only from our side.
//
// The position is a free-running int16 that wraps. Subtracting in int16_t makes the wrap a
// non-event: 32767 -> -32768 is a delta of +1, which is what actually happened.
#include "knob.h"

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/board.h"
#include "i2c_bus.h"

// Set to 1 to print raw position/delta on every change. This is the tool for settling
// kCountsPerDetent on real hardware (see below) — the same role WAKE_DEBUG plays for the wake word.
#define KNOB_DEBUG 0

// THE ADDRESSES ARE DOCUMENTED 8-BIT; Arduino's Wire takes 7-BIT. Arduino's own datasheet and our
// hardware/jukebox-7/README.md both say "0x76" (with 0x74 as the alternate), but a bus scan with
// the dial actually attached found it at **0x3A** — which is 0x74 >> 1. Probing only the 8-bit
// forms found nothing at all and the driver reported "no dial on the bus" with the dial plugged in
// and working. Both conventions are listed here so either wins; four probes at boot is nothing.
//   0x3A = 0x74 >> 1 (the one this unit actually answers on)
//   0x3B = 0x76 >> 1
static const uint8_t kAddrs[] = {0x3A, 0x3B, 0x76, 0x74};

// How many reported counts make one physical detent. **VERIFY ON HARDWARE.** The Bourns
// PEC11J-9215F-S0015 is 15 PPR / 30 detents, so the underlying quadrature is 2 edges per detent —
// but the Modulino's STM32 may already divide that down before reporting. 1 is the assumption that
// the firmware reports detents directly. If one click of the dial moves volume by 2, set this to 2;
// build with KNOB_DEBUG 1 and turn the dial one click to read the answer straight off the log.
static const int32_t kCountsPerDetent = 1;

static const uint32_t kPollMs   = 20;    // 50 Hz. Position is accumulated by the device, so this
                                         // rate cannot lose counts — it only bounds how quickly a
                                         // press is noticed.
static const uint32_t kLongMs   = 700;   // shorter than the nest's 1000: that number exists for the
                                         // nest's stiff K112 tact switch, and this is a light
                                         // encoder-shaft switch that presses cleanly.
// Rescan interval while the dial is absent. 30 s, not 3 s, purely to bound console noise: a failed
// requestFrom on this framework prints two log_e lines that we cannot suppress locally
// ("i2cRead returned Error 259 / ESP_ERR_INVALID_STATE"), and the address-only pre-gate does not
// reliably short-circuit it from a task. This noise exists ONLY while nothing is plugged into J13
// and disappears entirely once the dial answers, so it is a bring-up annoyance rather than a
// running cost. 30 s still finds a dial plugged in live, without a reflash.
static const uint32_t kProbeMs  = 30000;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static uint8_t   s_addr     = 0;         // 0 = not present
static int32_t   s_rawAccum = 0;         // counts pending, drained by encoderDelta()
static KnobEvent s_event    = KnobEvent::None;
static bool      s_down     = false;

static int16_t   s_lastPos  = 0;
static bool      s_seeded   = false;     // first read seeds the position without emitting a delta
static uint32_t  s_pressMs  = 0;
static bool      s_longFired = false;

// Address-only write. Used ONLY as a cheap first gate, for one reason: on this framework
// endTransmission() does not log a plain NACK, whereas requestFrom() calls log_e() on every
// failure. Probing an absent dial with a read therefore floods the console with
// "i2cRead returned Error 259 (ESP_ERR_INVALID_STATE)" every rescan, forever. This is silent.
//
// It is NOT sufficient on its own — it is what originally "found" a Modulino at 0x74 on an empty
// bus. Neither probe style is trustworthy alone from a task once a previous transaction has
// failed; the validated read below is what actually decides.
static bool ackProbe(uint8_t addr) {
  if (!i2cBusLock()) return false;
  Wire.beginTransmission(addr);
  const bool ok = (Wire.endTransmission() == 0);
  i2cBusUnlock();
  return ok;
}

// The real test: four bytes that actually arrived, and are not the phantom described below.
static bool probe(uint8_t addr) {
  if (!i2cBusLock()) return false;
  const size_t got = Wire.requestFrom(addr, (size_t)4);
  uint8_t b[4] = {0, 0, 0, 0};
  size_t n = 0;
  while (Wire.available() && n < 4) b[n++] = (uint8_t)Wire.read();
  while (Wire.available()) Wire.read();
  i2cBusUnlock();
#if KNOB_DEBUG
  if (got) Serial.printf("[knob  ] probe 0x%02X: requestFrom=%u avail=%u bytes %02X %02X %02X %02X\n",
                         addr, (unsigned)got, (unsigned)n, b[0], b[1], b[2], b[3]);
#endif
  // THE PHANTOM: requestFrom() to a NACKing address still reports 4 readable bytes, because it
  // hands back the STALE RX BUFFER rather than failing. With nothing ever read the buffer is zeros,
  // which is why an empty bus produced a bogus "dial found at 0x74" that flapped every 3 s. With a
  // real dial present the same probe of an absent address returns a COPY OF THE DIAL'S LAST REPLY —
  // verified live: 0x3B/0x76/0x74 all answered "74 99 00 00" while only 0x3A actually ACKed.
  //
  // So the byte content is never proof of presence; the ACK is (see probeConfirmed). The all-zero
  // test below remains as a cheap second net for the cold-start case. A genuine reply's byte 0 is
  // the pinstrap — measured 0x74 on this unit, i.e. the 8-bit form of its 7-bit address 0x3A — so
  // it is never zero, not even for an idle dial at position 0 with the button up.
  if (got != 4 || n != 4) return false;
  return !(b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0);
}

// The phantom alternates with the failure it follows, so one hit proves nothing. A real dial
// answers twice; the phantom cannot. Belt and braces alongside the all-zero test above, because
// this is the one piece of the driver that cannot be verified until the cable arrives.
static bool probeConfirmed(uint8_t addr) {
  if (!ackProbe(addr)) return false;   // silent, and short-circuits the common "no dial" case
  if (!probe(addr)) return false;
  vTaskDelay(pdMS_TO_TICKS(5));
  return probe(addr);
}

// One 4-byte read. False if the dial did not answer, which is how a yanked cable is noticed.
static bool readState(int16_t &pos, bool &pressed) {
  if (!i2cBusLock()) return false;
  if (Wire.requestFrom(s_addr, (size_t)4) != 4) { i2cBusUnlock(); return false; }
  const uint8_t pinstrap = (uint8_t)Wire.read();   // not data — see the header comment
  const uint8_t lo = (uint8_t)Wire.read();
  const uint8_t hi = (uint8_t)Wire.read();
  const uint8_t btn = (uint8_t)Wire.read();
  i2cBusUnlock();
  // Same phantom guard as probe(): an all-zero reply is the driver's, not the dial's.
  if (pinstrap == 0 && lo == 0 && hi == 0 && btn == 0) return false;
  pressed = btn != 0;
  pos = (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
  return true;
}

static void knobTask(void *) {
  uint32_t lastProbe = 0;
  for (;;) {
    if (!s_addr) {
      // Absent: rescan slowly so the dial can be plugged in later without a reflash.
      if (millis() - lastProbe >= kProbeMs) {
        lastProbe = millis();
        for (uint8_t a : kAddrs) {
          if (!probeConfirmed(a)) continue;
          s_addr = a;
          s_seeded = false;
          Serial.printf("[knob  ] Modulino Knob found at 0x%02X\n", a);
          break;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(kPollMs));
      continue;
    }

    int16_t pos = 0;
    bool    pressed = false;
    if (!readState(pos, pressed)) {
      Serial.println("[knob  ] dial stopped answering — rescanning");
      portENTER_CRITICAL(&s_mux);
      s_addr = 0; s_down = false; s_event = KnobEvent::None;
      portEXIT_CRITICAL(&s_mux);
      vTaskDelay(pdMS_TO_TICKS(kPollMs));
      continue;
    }

    if (!s_seeded) {
      // Adopt wherever the dial happens to be sitting. Without this, the first poll after boot
      // reports the device's whole accumulated position as one enormous delta.
      s_seeded  = true;
      s_lastPos = pos;
      s_pressMs = 0;
      s_longFired = false;
    }

    const int16_t delta = (int16_t)(pos - s_lastPos);   // int16 subtraction: wrap-safe
    s_lastPos = pos;

#if KNOB_DEBUG
    if (delta) Serial.printf("[knob  ] pos=%d delta=%d pressed=%d\n", (int)pos, (int)delta, (int)pressed);
#endif

    const uint32_t now = millis();
    portENTER_CRITICAL(&s_mux);
    if (delta) s_rawAccum += delta;

    // Press classification, mirroring the nest's Short/Long split. No debounce pass: the STM32
    // reports a already-clean level, and a 20 ms poll is itself the debounce.
    if (pressed != s_down) {
      s_down = pressed;
      if (pressed) {
        s_pressMs   = now;
        s_longFired = false;
      } else if (!s_longFired) {
        s_event = KnobEvent::Short;      // released before the long threshold
      }
    }
    if (s_down && !s_longFired && (now - s_pressMs) >= kLongMs) {
      s_longFired = true;
      s_event = KnobEvent::Long;         // fire while still held, for immediate feedback
    }
    portEXIT_CRITICAL(&s_mux);

    vTaskDelay(pdMS_TO_TICKS(kPollMs));
  }
}

#if KNOB_DEBUG
// One-shot census of the bus. Two probe styles side by side, because they disagree: the
// write-probe is the one that invents devices.
static void busScan() {
  Serial.println("[knob  ] bus scan (W = address-only write ACK, R = 4-byte read returned 4)");
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    if (!i2cBusLock()) continue;
    Wire.beginTransmission(a);
    const bool w = (Wire.endTransmission() == 0);
    const bool r = (Wire.requestFrom(a, (size_t)4) == 4);
    while (Wire.available()) Wire.read();
    i2cBusUnlock();
    if (w || r) Serial.printf("[knob  ]   0x%02X  W=%d R=%d\n", a, (int)w, (int)r);
  }
  Serial.println("[knob  ] scan done");
}
#endif

bool knobInit() {
#if KNOB_DEBUG
  busScan();
#endif
  for (uint8_t a : kAddrs) {
    if (!probeConfirmed(a)) continue;
    s_addr = a;
    break;
  }
  if (s_addr) Serial.printf("[knob  ] Modulino Knob at 0x%02X\n", s_addr);
  else        Serial.println("[knob  ] no dial on the bus — will keep looking (plug it in any time)");

  // Core 0, with the rest of the I/O: the bus is shared with the GT911, which LVGL reads on the UI
  // task. arduino-esp32's Wire holds a per-bus mutex so the two cannot interleave mid-transaction,
  // but a blocking I2C read still has no business on the render task.
  if (xTaskCreatePinnedToCore(knobTask, "knob", 3072, nullptr, 1, nullptr, 0) != pdPASS) {
    Serial.println("[knob  ] could not start the poll task");
    return false;
  }
  return s_addr != 0;
}

// --- core/board.h ---------------------------------------------------------------------------------
// All four are cheap non-blocking reads of state the poll task maintains; no I2C happens here, so
// the UI task may call them every tick.

int32_t encoderDelta() {
  portENTER_CRITICAL(&s_mux);
  const int32_t raw = s_rawAccum;
  const int32_t det = raw / kCountsPerDetent;
  s_rawAccum = raw - det * kCountsPerDetent;   // keep the sub-detent remainder, as the nest does
  portEXIT_CRITICAL(&s_mux);
  return det;
}

KnobEvent knobEvent() {
  portENTER_CRITICAL(&s_mux);
  const KnobEvent e = s_event;
  s_event = KnobEvent::None;
  portEXIT_CRITICAL(&s_mux);
  return e;
}

bool knobPressed() { return knobEvent() == KnobEvent::Short; }

bool knobDown() { return s_down; }

// --- diagnostics ----------------------------------------------------------------------------------
String knobDiagJson() {
  String o = "{";
  o += "\"addr\":\"";
  { char b[8]; snprintf(b, sizeof b, "0x%02X", s_addr); o += b; }
  o += "\",\"found\":";      o += (s_addr ? "true" : "false");
  o += ",\"seeded\":";        o += (s_seeded ? "true" : "false");
  o += ",\"lastPos\":";       o += String((int)s_lastPos);
  o += ",\"pendingCounts\":"; o += String((int)s_rawAccum);
  o += ",\"down\":";          o += (s_down ? "true" : "false");
  o += ",\"countsPerDetent\":"; o += String((int)kCountsPerDetent);

  // Live probe of every address we look for, reporting the RAW bytes. Byte 0 should be the
  // Modulino's pinstrap; if a real dial answers all-zero then the phantom guard in probe() is
  // what is rejecting it, and this is the only way to see that from off-device.
  o += ",\"probe\":[";
  for (size_t i = 0; i < sizeof kAddrs; ++i) {
    uint8_t b[4] = {0, 0, 0, 0};
    size_t n = 0, got = 0;
    bool ack = false;
    if (i2cBusLock()) {
      Wire.beginTransmission(kAddrs[i]);
      ack = (Wire.endTransmission() == 0);
      got = Wire.requestFrom(kAddrs[i], (size_t)4);
      while (Wire.available() && n < 4) b[n++] = (uint8_t)Wire.read();
      while (Wire.available()) Wire.read();
      i2cBusUnlock();
    }
    char buf[160];
    snprintf(buf, sizeof buf,
             "%s{\"addr\":\"0x%02X\",\"ack\":%s,\"got\":%u,\"read\":%u,"
             "\"bytes\":\"%02X %02X %02X %02X\"}",
             i ? "," : "", kAddrs[i], ack ? "true" : "false", (unsigned)got, (unsigned)n,
             b[0], b[1], b[2], b[3]);
    o += buf;
  }
  o += "]";

  // NO FULL BUS CENSUS HERE. An ACK probe to an ABSENT address blocks on the I2C timeout (~80 ms),
  // so sweeping 0x08-0x77 took 9.3 s and the HTTP client simply saw the connection die. The census
  // lives at boot instead, behind KNOB_DEBUG, where it is uncontended and nothing is waiting on it —
  // and it is what found this dial answering on 0x3A when the driver was only looking at 0x76/0x74.
  o += "}";
  return o;
}
