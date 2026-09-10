// See imu.h.
//
// SOFTWARE tap detection, not the QMI8658's own CTRL8 motion engine. Two reasons, both practical:
// the part's tap interrupt reports on an INT line this board does not document anywhere (not the
// wiki, not the vendor demo, which exposes neither CTRL8 nor CTRL9 beyond a #define), and a
// threshold we own is one we can retune from the config page later without a datasheet hunt.
// If this ever proves too jumpy on a printed case, the hardware engine is the upgrade path — but
// it needs the schematic first.
#include "imu.h"
#include "pins.h"
#include <Arduino.h>
#include <Wire.h>
#include "core/net/logmirror.h"     // LOG — this unit is headless; serial alone is invisible

// Set to 1 to print a live jerk peak every second. THIS IS THE TUNING TOOL, and the threshold
// below came out of it. Same role WAKE_DEBUG plays in boards/es3c28p/wake_word.cpp, and the same
// advice applies: check the IDLE FLOOR before concluding a threshold is wrong.
//
// That advice earned itself immediately. The first hardware run showed a bimodal resting signal —
// a 53-70 floor plus a second mode sitting constantly at 250-300 — and the tempting explanation
// was the 500 Hz ODR beating against the ~200 Hz poll, i.e. differencing non-adjacent samples.
// It was not: a second capture with the board genuinely untouched showed a clean 44-95 floor and
// no second mode at all. The first run was simply a board being handled. No data-ready gating is
// needed, and free-running the poll is fine.
#define TAP_DEBUG 0

// --- Registers (vendor demo Gyro_QMI8658.h, which agrees with the datasheet) ---
static const uint8_t REG_WHO_AM_I = 0x00;
static const uint8_t REG_CTRL1    = 0x02;
static const uint8_t REG_CTRL2    = 0x03;
static const uint8_t REG_CTRL7    = 0x08;
static const uint8_t REG_AX_L     = 0x35;

static const uint8_t WHO_AM_I_VAL = 0x05;   // fixed device id

// CTRL1 bit6 = ADDR_AI, address auto-increment. Without it the 6-byte burst read below returns the
// SAME register six times — which looks like a live sensor reporting a constant, not like an error.
static const uint8_t CTRL1_ADDR_AI = 0x40;

// CTRL2: bits[6:4] full scale, bits[3:0] ODR.
//   aFS 010 = +/-8 g. A deliberate knock peaks well past 4 g; clipping the peak is what turns a
//   sharp tap into an indistinct one, so there is no reason to run tighter.
//   aODR 0100 = 500 Hz. The impulse from a fingertip on a plastic wall rings for only a few ms,
//   and an ODR near the poll rate would alias it away entirely.
static const uint8_t CTRL2_8G_500HZ = 0x24;

// ⚠️⚠️ CTRL6 = 0 (ATTITUDE ENGINE OFF) IS LOAD-BEARING. Without it this part accepts every other
// register, reports them back correctly, and produces NOTHING: STATUSINT stays 0x00 and all three
// axes sit pinned at 0x7FFF. With the AttitudeEngine running the raw accel registers simply are
// not updated — it emits fused output instead — so the failure looks nothing like a configuration
// error and everything like a dead sensor.
//
// It bites only on a board still holding Waveshare's FACTORY DEMO state, because the IMU is not
// power-cycled by an ESP32 reset and keeps whatever mode the last firmware left it in. The first
// board here happened to come up with AE off and worked on a config that omitted CTRL6 entirely;
// the replacement did not, and the identical firmware read 0x7FFF forever.
//
// Isolated by bisection, not assumed: with CTRL6 cleared this works at CTRL7 = 0x01, so the gyro
// (0x43) and the "high speed internal clock" bit (0x41) that Waveshare's own driver sets are both
// unnecessary here. Enabling the gyro would have cost 2-3 mA for nothing.
static const uint8_t REG_CTRL6      = 0x07;   // ⚠️ 0x07. 0x06 is CTRL5 — an off-by-one here writes
                                              // the LPF register and leaves AE running.
static const uint8_t CTRL6_AE_OFF   = 0x00;
static const uint8_t CTRL7_ACC_ONLY = 0x01;

// --- Detection tuning --------------------------------------------------------------------
// Poll cadence. uiTick runs at ~5 ms, and a 6-byte read at 400 kHz costs ~150 us, so sampling
// every tick spends ~3% of it. Worth it: at 500 Hz ODR the sensor has a fresh sample nearly every
// time, and a slower poll would step straight over the impulse it is looking for.
static const uint32_t POLL_MS      = 5;

// One knock rings for tens of ms and would otherwise latch several times. Also long enough that
// the shock of a BUTTON press does not queue a second wake behind the first.
static const uint32_t REFRACTORY_MS = 400;

// Ignore everything for this long after init: powering the rail and seating the part both settle
// through the accelerometer, and a phantom tap at boot would light the screen for no reason.
static const uint32_t SETTLE_MS    = 750;

// MEASURED IN THE PRINTED CASE, ON A DESK, 2026-09-10 — 139 s in four phases:
//
//   hands off the desk        max     290
//   TYPING on the same desk   max   1,688   (top five: 1325 1328 1383 1494 1688)
//   deliberate taps           29,814 · 56,335 · 60,792 · 65,486 · 78,840 · 80,823
//
// A 17.7x band with nothing whatsoever inside it, so 7000 — the geometric midpoint — sits 4.1x
// above the worst typing spike and 4.3x below the weakest real tap. Balanced on BOTH sides rather
// than merely clearing one, which matters because the two failure modes are equally bad: a screen
// that wakes when someone types, and a tap that does nothing.
//
// ⚠️ THE OLD VALUE OF 1200 CAME FROM A BARE BOARD AND WAS NOT TRANSFERABLE. On a loose PCB, taps
// read 3,674-80,123 against an idle floor of 44-95; bolted into a case on a desk BOTH sides moved
// and not by the same factor. The case is a far better mechanical path to the surface — typing
// went from invisible to 1,688, right through the old threshold, which is exactly the reported
// symptom — while also transmitting a fingertip tap better, lifting the weakest tap from 3,674 to
// 29,814. Re-measure in the enclosure, never on the bench.
//
// The gap is wide enough that a plain threshold is sufficient; discriminating on shape (a sharp
// impulse against a quiet background, versus sustained chatter) was considered and is unnecessary.
static const int32_t  TAP_JERK_LSB = 7000;

static uint8_t  s_addr      = 0;
static bool     s_have      = false;
static uint32_t s_nextPoll  = 0;
static uint32_t s_readyAt   = 0;
static uint32_t s_lastTap   = 0;
static bool     s_havePrev  = false;
static int16_t  s_px, s_py, s_pz;

static bool wr(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// ⚠️ The ACK is the proof a device is there, never the bytes. A NACKed requestFrom() on this
// platform hands back the STALE RX BUFFER — a copy of the last good reply, or zeros on a cold bus
// — which is exactly how the jukebox invented a phantom rotary dial on an empty bus (CLAUDE.md).
// So: check endTransmission(), then check available(), and treat a short read as a failure.
static bool rd(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)s_addr, (int)len) != len) return false;
  for (uint8_t i = 0; i < len; ++i) {
    if (!Wire.available()) return false;
    buf[i] = Wire.read();
  }
  return true;
}

bool imuInit() {
  const uint8_t candidates[2] = { QMI8658_ADDR_LOW, QMI8658_ADDR_HIGH };
  for (uint8_t i = 0; i < 2; ++i) {
    s_addr = candidates[i];
    Wire.beginTransmission(s_addr);
    if (Wire.endTransmission() != 0) continue;         // no ACK — nothing at this address

    uint8_t who = 0;
    if (!rd(REG_WHO_AM_I, &who, 1) || who != WHO_AM_I_VAL) {
      LOG.printf("[imu    ] 0x%02X ACKed but WHO_AM_I=0x%02X (want 0x%02X) — not a QMI8658\n",
                 s_addr, who, WHO_AM_I_VAL);
      continue;
    }

    if (!wr(REG_CTRL1, CTRL1_ADDR_AI) ||
        !wr(REG_CTRL2, CTRL2_8G_500HZ) ||
        !wr(REG_CTRL6, CTRL6_AE_OFF) ||
        !wr(REG_CTRL7, CTRL7_ACC_ONLY)) {
      LOG.printf("[imu    ] 0x%02X found but configuration write failed\n", s_addr);
      continue;
    }

    s_have     = true;
    s_readyAt  = millis() + SETTLE_MS;
    s_havePrev = false;
    LOG.printf("[imu    ] QMI8658 @ 0x%02X — accel +/-8g @ 500 Hz, tap wake armed\n", s_addr);
    return true;
  }

  s_addr = 0;
  s_have = false;
  // Not fatal, and worth saying plainly: the button press still wakes the screen, so the unit is
  // fully usable without this. Only knock-to-wake is gone.
  LOG.println("[imu    ] no QMI8658 on the bus — tap wake disabled (button press still wakes)");
  return false;
}

uint8_t imuAddress() { return s_addr; }

bool imuTapDetected() {
  if (!s_have) return false;

  const uint32_t now = millis();
  if ((int32_t)(now - s_nextPoll) < 0) return false;   // signed: survives the 49.7-day rollover
  s_nextPoll = now + POLL_MS;

  uint8_t b[6];
  if (!rd(REG_AX_L, b, 6)) return false;

  const int16_t ax = (int16_t)((uint16_t)b[1] << 8 | b[0]);
  const int16_t ay = (int16_t)((uint16_t)b[3] << 8 | b[2]);
  const int16_t az = (int16_t)((uint16_t)b[5] << 8 | b[4]);

  int32_t jerk = 0;
  if (s_havePrev) {
    // Sum of absolute per-axis change. Gravity is common to both samples and cancels, so this
    // needs no baseline tracking and no orientation assumption — the box can be mounted any way up.
    jerk = (int32_t)abs((int)ax - s_px) + abs((int)ay - s_py) + abs((int)az - s_pz);
  }
  s_px = ax; s_py = ay; s_pz = az;
  s_havePrev = true;

#if TAP_DEBUG
  {
    static int32_t  peak = 0;
    static uint32_t next = 0;
    if (jerk > peak) peak = jerk;
    if ((int32_t)(now - next) >= 0) {
      next = now + 1000;
      LOG.printf("[imu    ] jerk peak %ld (threshold %ld) a=%d,%d,%d\n",
                 (long)peak, (long)TAP_JERK_LSB, ax, ay, az);
      peak = 0;
    }
  }
#endif

  if ((int32_t)(now - s_readyAt) < 0) return false;    // still settling after boot
  if (jerk < TAP_JERK_LSB) return false;
  if (s_lastTap && (now - s_lastTap) < REFRACTORY_MS) return false;

  s_lastTap = now;
  return true;
}
