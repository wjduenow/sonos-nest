#include "bringup.h"
#include "../board_pins.h"
#include "display.h"
#include "encoder.h"
#include "pcf8574.h"
#include "touch.h"
#include <Arduino.h>
#include <Wire.h>

static void i2cScan() {
  Serial.println("[bringup] I2C scan:");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      const char *tag = "";
      if (addr == PCF8574_ADDR) tag = "  (expected PCF8574)";
      else if (addr == TOUCH_ADDR) tag = "  (expected CST816 touch)";
      Serial.printf("  - device @ 0x%02X%s\n", addr, tag);
      found++;
    }
  }
  if (!found) Serial.println("  (none found — check SDA/SCL wiring)");
}

void bringupRun() {
  Serial.println("\n===== Phase 0 bring-up =====");

  // 0. Memory report — the 480x480x2 framebuffer (~460 KB) requires PSRAM. If PSRAM size
  //    is 0, the OPI-PSRAM build config didn't take and the display will fail to allocate.
  Serial.printf("[bringup] PSRAM: %u bytes total, %u free\n",
                (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());
  Serial.printf("[bringup] internal heap free: %u bytes\n", (unsigned)ESP.getFreeHeap());
  if (ESP.getPsramSize() == 0) Serial.println("[bringup] WARNING: no PSRAM — display alloc will fail");

  // 1. PCF8574 expander (also brings up the I2C bus).
  bool pcfOk = pcf8574Init();
  Serial.printf("[bringup] PCF8574 @ 0x%02X: %s\n", PCF8574_ADDR,
                pcfOk ? "ACK ok" : "NO ACK (FAIL)");
  i2cScan();

  // 2. Display: power/init, then RGB test pattern (R/G/B/W quadrants) to confirm the
  //    data pin map + color order. Wrong pins => wrong or garbled colors.
  bool dispOk = displayInit();
  Serial.printf("[bringup] display init: %s\n", dispOk ? "ok" : "FAIL (alloc/begin)");
  if (dispOk) {
    Serial.println("[bringup] test pattern: expect TL=red TR=green BL=blue BR=white");
    displayTestPattern();
  }

  // 3. Backlight ramp — eyeball brightness sweeping up/down.
  Serial.println("[bringup] backlight ramp...");
  for (int p = 100; p >= 0; p -= 5) { backlightSet(p); delay(15); }
  for (int p = 0; p <= 100; p += 5) { backlightSet(p); delay(15); }
  backlightSet(100);

  // 4. Touch + encoder live readout.
  touchInit();
  encoderInit();
  Serial.println("[bringup] TWIST + PRESS the knob, and TOUCH the screen. Live readout:");
  Serial.println("          (verify twist sign, 1 click = 1 detent, press once per push,");
  Serial.println("           and touch coords are in range 0..479)");

  int32_t total = 0;
  uint32_t lastPrint = 0;
  for (;;) {
    int32_t d = encoderDelta();
    if (d != 0) {
      total += d;
      Serial.printf("[encoder] delta=%+ld total=%ld\n", (long)d, (long)total);
    }
    if (knobPressed()) Serial.println("[button ] PRESS");

    uint16_t tx, ty;
    if (touchRead(tx, ty)) Serial.printf("[touch  ] x=%u y=%u\n", tx, ty);

    if (millis() - lastPrint > 3000) {
      lastPrint = millis();
      Serial.printf("[alive  ] held=%d total=%ld\n", knobDown() ? 1 : 0, (long)total);
    }
    delay(2);
  }
}
