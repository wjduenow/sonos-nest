// ESP32-S3-LCD-1.47B Phase-0 bring-up — the first code to run on the button-v3 board.
//
// Standalone: this env links neither core/ nor a unit, so everything here is self-contained and
// prints to Serial rather than LOG (the mirror lives in core/net/). That is the established shape
// for bring-ups in this tree — they run with a cable attached anyway.
//
// It answers, in the order that they would block later phases:
//
//   1. Did the build config take? This is a 16 MB-flash / 8 MB-PSRAM board, unlike both other
//      buttons. PSRAM 0 or flash 8 MB means the env is wrong and everything downstream is on sand.
//   2. Does the PANEL come up, and is the 34-px column offset right? The offset is the one
//      constant on this board that fails without erroring: get it wrong and the picture still
//      draws, just shifted, with a band of garbage at one edge — which reads as "bad panel". So
//      the test pattern is a 1-px border hard against all four edges. If any edge is missing or
//      doubled, the offset is wrong; nothing else on this screen tells you that.
//   3. Is the QMI8658 there, and at which of its two addresses? The datasheet quotes 8-bit
//      addresses (0xD6/0xD4); Wire is 7-bit. Both candidates are probed and WHO_AM_I is verified,
//      because an ACK alone is not proof — a NACKed requestFrom() on this platform returns the
//      stale RX buffer, which is how the jukebox once invented a dial on an empty bus.
//   4. Do the ring and the button work on GP4/GP2 — and, the question that could have forced a
//      BOM change, does the 5V pad actually drive the ring?
//   5. Live accelerometer jerk, so TAP_JERK_LSB in imu.cpp can be set from measurement rather
//      than from the guess it currently is.
#include "bringup.h"
#include "pins.h"
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>

static const uint32_t DEBOUNCE_MS = 30;

// Ring PWM, same channel and rate the app uses (board.cpp). INVERTED: the pin sinks the cathode,
// so duty 0 is fully lit and 255 is off. Every write goes through ringSet() so the inversion lives
// in exactly one place here, as it does in the board HAL.
static const int      RING_CH   = 0;
static const uint32_t RING_FREQ = 5000;
static const uint8_t  RING_RES  = 8;
static inline void ringSet(uint8_t pct) {
  if (pct > 100) pct = 100;
  ledcWrite(RING_CH, 255 - ((uint32_t)pct * 255 / 100));
}

static Arduino_DataBus *bus = nullptr;
static Arduino_GFX     *gfx = nullptr;

#if (DISPLAY_ROTATION & 1)
  static const int16_t W = LCD_HEIGHT, H = LCD_WIDTH;
#else
  static const int16_t W = LCD_WIDTH,  H = LCD_HEIGHT;
#endif

static void panelTest() {
  Serial.println("\n[2] panel");
  bus = new Arduino_ESP32SPI(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI, GFX_NOT_DEFINED);
  gfx = new Arduino_ST7789(bus, PIN_LCD_RST, DISPLAY_ROTATION, LCD_INVERT_COLORS,
                           LCD_WIDTH, LCD_HEIGHT,
                           LCD_COL_OFFSET, LCD_ROW_OFFSET, LCD_COL_OFFSET, LCD_ROW_OFFSET);
  gfx->begin();

  ledcSetup(1, 5000, 8);
  ledcAttachPin(PIN_LCD_BL, 1);
  ledcWrite(1, 255);

  gfx->fillScreen(BLACK);

  // Colour bars — proves data lines and byte order. If red and blue swap, LCD_INVERT_COLORS/ips
  // is the knob.
  const uint16_t bars[6] = { RED, GREEN, BLUE, CYAN, MAGENTA, YELLOW };
  const int16_t  bw = W / 6;
  for (int i = 0; i < 6; ++i) gfx->fillRect(i * bw, 0, bw, H / 2, bars[i]);

  // THE OFFSET TEST. A 1-px white border hard against the panel edges. All four visible and
  // single => LCD_COL_OFFSET is right. Missing on one side, or a coloured band beyond it => wrong.
  gfx->drawRect(0, 0, W, H, WHITE);

  gfx->setTextColor(WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(6, H / 2 + 8);
  gfx->printf("%dx%d rot=%d", W, H, DISPLAY_ROTATION);
  gfx->setCursor(6, H / 2 + 20);
  gfx->print("border: 4 edges = offset OK");

  Serial.printf("    logical %dx%d (rotation %d), col offset %d\n",
                W, H, DISPLAY_ROTATION, LCD_COL_OFFSET);
  Serial.println("    LOOK AT THE GLASS: a 1-px white border must touch all four edges.");
}

// Probe an address the only way that means anything: ACK first, then a register read that has a
// known answer.
static uint8_t imuProbe() {
  const uint8_t cand[2] = { QMI8658_ADDR_LOW, QMI8658_ADDR_HIGH };
  for (int i = 0; i < 2; ++i) {
    Wire.beginTransmission(cand[i]);
    const uint8_t err = Wire.endTransmission();
    Serial.printf("    0x%02X: %s\n", cand[i], err == 0 ? "ACK" : "no ACK");
    if (err) continue;

    Wire.beginTransmission(cand[i]);
    Wire.write((uint8_t)0x00);                 // WHO_AM_I
    if (Wire.endTransmission(false) != 0) continue;
    if (Wire.requestFrom((int)cand[i], 1) != 1) continue;
    const uint8_t who = Wire.read();
    Serial.printf("      WHO_AM_I = 0x%02X (%s)\n", who, who == 0x05 ? "QMI8658" : "UNEXPECTED");
    if (who == 0x05) return cand[i];
  }
  return 0;
}

static bool imuStart(uint8_t addr) {
  // ⚠️ BIT 6 IS THE HIGH-SPEED INTERNAL CLOCK, AND WITHOUT IT THE PART DOES NOT SAMPLE.
  // 0x01 (aEN alone) leaves CTRL1/CTRL2/CTRL7 all reading back correctly, STATUSINT at 0x00 and
  // every axis pinned at 0x7FFF — configured, enabled, and producing nothing. Waveshare's own
  // driver writes 0x43 and comments bit 6 as "enable high speed internal clock"; 0x41 is the same
  // thing with the gyro left off, which this unit has no use for (~2-3 mA saved).
  // CTRL6 = 0 disables the AttitudeEngine, which the factory demo may have left running.
  struct { uint8_t reg, val; } init[] = {
    { 0x02, 0x40 },   // CTRL1: ADDR_AI on; bit0 clear = 2 MHz oscillator enabled
    { 0x03, 0x24 },   // CTRL2: accel +/-8 g @ 500 Hz
    { 0x07, 0x00 },   // CTRL6: ATTITUDE ENGINE OFF — see the note above. This is the one.
    { 0x08, 0x01 },   // CTRL7: accelerometer only
  };
  for (auto &w : init) {
    Wire.beginTransmission(addr);
    Wire.write(w.reg); Wire.write(w.val);
    if (Wire.endTransmission() != 0) return false;
  }
  return true;
}

static bool imuRead(uint8_t addr, int16_t &ax, int16_t &ay, int16_t &az) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)0x35);                   // AX_L
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, 6) != 6) return false;
  uint8_t b[6];
  for (int i = 0; i < 6; ++i) b[i] = Wire.read();
  ax = (int16_t)((uint16_t)b[1] << 8 | b[0]);
  ay = (int16_t)((uint16_t)b[3] << 8 | b[2]);
  az = (int16_t)((uint16_t)b[5] << 8 | b[4]);
  return true;
}

void waveshareBringupRun() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n\n=== ESP32-S3-LCD-1.47B bring-up (button-v3) ===");

  // --- 1. Build config -------------------------------------------------------------------
  Serial.println("\n[1] silicon + build config");
  Serial.printf("    chip     %s rev %d, %d MHz, %d core(s)\n",
                ESP.getChipModel(), ESP.getChipRevision(), getCpuFrequencyMhz(), ESP.getChipCores());
  Serial.printf("    flash    %u MB   %s\n", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)),
                ESP.getFlashChipSize() == 16 * 1024 * 1024 ? "OK" : "*** EXPECTED 16 MB ***");
  Serial.printf("    PSRAM    %u KB   %s\n", (unsigned)(ESP.getPsramSize() / 1024),
                ESP.getPsramSize() > 0 ? "OK" : "*** ZERO — env is wrong ***");
  Serial.printf("    heap     %u KB free\n", (unsigned)(ESP.getFreeHeap() / 1024));

  // --- 2. Panel --------------------------------------------------------------------------
  panelTest();

  // --- 3. IMU ----------------------------------------------------------------------------
  Serial.println("\n[3] QMI8658 on I2C");
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
  const uint8_t imu = imuProbe();
  if (imu && imuStart(imu)) {
    Serial.printf("    configured at 0x%02X\n", imu);

    // ⚠️ Read the control registers BACK, and read the data registers BOTH ways. A burst that
    // returns 0xFF on every byte is an idle bus, not a measurement — and the single-byte path
    // separates the two causes that produce it: config that never took (CTRL7 reads 0), versus
    // address auto-increment not working (single reads fine, burst all 0xFF).
    struct { uint8_t reg; const char *name; } regs[] = {
      {0x02, "CTRL1 (want 0x40 ADDR_AI)"}, {0x03, "CTRL2 (want 0x24)"},
      {0x08, "CTRL7 (want 0x01 aEN)"},     {0x2D, "STATUSINT"},
    };
    for (auto &r : regs) {
      uint8_t v = 0xEE;
      Wire.beginTransmission(imu); Wire.write(r.reg);
      if (Wire.endTransmission(false) == 0 && Wire.requestFrom((int)imu, 1) == 1) v = Wire.read();
      Serial.printf("      0x%02X = 0x%02X  %s\n", r.reg, v, r.name);
    }
    Serial.print("      AX_L..AZ_H one byte at a time:");
    for (uint8_t r = 0x35; r <= 0x3A; ++r) {
      uint8_t v = 0xEE;
      Wire.beginTransmission(imu); Wire.write(r);
      if (Wire.endTransmission(false) == 0 && Wire.requestFrom((int)imu, 1) == 1) v = Wire.read();
      Serial.printf(" %02X", v);
    }
    Serial.println();
  }
  else if (imu)             Serial.println("    found but configuration FAILED");
  else                      Serial.println("    *** not found — tap wake will be unavailable ***");

  // --- 4. RGB bead -----------------------------------------------------------------------
  // Built into the Arduino core (esp32-hal-rgb-led), so no library. Inside the case once
  // assembled, so this is the last time it is ever useful.
  Serial.println("\n[4] WS2812 bead — red, green, blue, off");
  const uint8_t rgb[4][3] = { {32,0,0}, {0,32,0}, {0,0,32}, {0,0,0} };
  for (auto &c : rgb) { neopixelWrite(PIN_RGB_LED, c[0], c[1], c[2]); delay(400); }

  // --- 5. Ring ---------------------------------------------------------------------------
  // Set up only; the real test is in the loop below, where it responds to the button. A one-shot
  // fade at boot is no good for wiring work — you miss it, and re-running it means a power cycle
  // with a soldering iron in your hand.
  Serial.println("\n[5] ring on GP4, low-side off the 5V header pad");
  Serial.println("    Wiring:  white -> 5V(pad 1)   black -> GP4(pad 7)");
  Serial.println("             brown -> GP2(pad 5)  brown -> GND(pad 2)");
  Serial.println("    IF IT NEVER LIGHTS: check the 5V pad first, then the solder joints.");
  Serial.println("    Remember the logic is INVERTED — the pin SINKS the cathode, so LOW = lit.");
  ledcSetup(RING_CH, RING_FREQ, RING_RES);
  ledcAttachPin(PIN_RING_GATE, RING_CH);
  ringSet(0);                                     // start dark

  // --- 6. Button + live tap numbers ------------------------------------------------------
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  Serial.println("\n[6] button on GP2, ring on GP4, live accelerometer jerk");
  Serial.println("    The ring BREATHES while idle and goes FULL BRIGHT while the button is held.");
  Serial.println("    That tests both halves of the harness at once: if it breathes but does not");
  Serial.println("    respond, the switch is wrong; if it responds but never breathes, the PWM is.");
  Serial.printf("    idle level: %s (expect HIGH)\n", digitalRead(PIN_BUTTON) ? "HIGH" : "LOW");
  Serial.println("    Press the button. Knock the board. Both are reported below.");
  Serial.println("    Use the jerk peaks to set TAP_JERK_LSB in imu.cpp — note the IDLE floor");
  Serial.println("    as well as the knock peaks; the gap between them is the whole margin.");

  // Seed from the ACTUAL pin, not from a constant. Seeding these `true` (= pressed) made the very
  // first loop see a state change to "released" and report a phantom `press #1 — held 4210 ms`,
  // with the held time being however long the board had been up — on a board with nothing wired to
  // GP2 at all. button_common/button.cpp gets this right (`s_raw = s_stable = rawDown()`); this
  // file did not. Caught on the first hardware run, 2026-09-08.
  bool     raw = (digitalRead(PIN_BUTTON) == LOW), stable = raw;
  uint32_t lastChange = millis(), pressedAt = millis(), nextReport = millis() + 1000;
  int16_t  px = 0, py = 0, pz = 0;
  bool     havePrev = false;
  int32_t  peak = 0;
  uint32_t presses = 0;

  for (;;) {
    const uint32_t now = millis();

    const bool r = (digitalRead(PIN_BUTTON) == LOW);
    if (r != raw) { raw = r; lastChange = now; }
    if (r != stable && (now - lastChange) >= DEBOUNCE_MS) {
      stable = r;
      if (stable) { pressedAt = now; }
      else        { Serial.printf("    press #%lu — held %lu ms\n",
                                  (unsigned long)++presses, (unsigned long)(now - pressedAt)); }
    }

    // Ring: full bright while held, otherwise a slow breathe. Driven off the DEBOUNCED level, not
    // the raw pin, so a bouncing contact cannot make it flicker and send you hunting a PWM fault.
    if (stable) {
      ringSet(100);
    } else {
      // ~3 s period triangle. Deliberately never fully off at the bottom — "dark" and "not wired"
      // look identical, and you want to be able to tell them apart at a glance.
      const uint32_t phase = now % 3000;
      const uint32_t tri   = phase < 1500 ? phase : (3000 - phase);
      ringSet(10 + (uint8_t)(tri * 90 / 1500));
    }

    if (imu) {
      int16_t ax, ay, az;
      if (imuRead(imu, ax, ay, az)) {
        if (havePrev) {
          const int32_t jerk = abs((int)ax - px) + abs((int)ay - py) + abs((int)az - pz);
          if (jerk > peak) peak = jerk;
        }
        px = ax; py = ay; pz = az;
        havePrev = true;
      }
    }

    if ((int32_t)(now - nextReport) >= 0) {
      nextReport = now + 1000;
      // Print the RAW axes alongside the jerk. A jerk of 0 says only "nothing changed", which is
      // ambiguous between a dead sensor, a failed read, and a burst read returning the same
      // register six times (CTRL1 ADDR_AI not taking). The raw values separate all three: a live
      // part at rest reads ~4096 on one axis and near zero on the others, and dithers by an LSB.
      if (imu) {
        int16_t ax, ay, az;
        const bool ok = imuRead(imu, ax, ay, az);
        Serial.printf("    jerk peak %ld  |  raw %s a=%d,%d,%d  (1 g = 4096 at +/-8 g)\n",
                      (long)peak, ok ? "ok " : "FAIL", ax, ay, az);
      }
      peak = 0;
    }
    delay(5);
  }
}
