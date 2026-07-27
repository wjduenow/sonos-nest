// Screen bring-up for the ELECROW CrowPanel Advance 7" ESP32-P4 (env: jukebox-bringup).
//
// Deliberately standalone: no core/, no unit, no networking. The jukebox is the first
// ESP32-P4 board in this project, on a different toolchain (pioarduino / Arduino 3.3.x /
// IDF 5.5), so this file answers the only question that matters first — does the panel
// light up and does LVGL render on it — before anything else is ported.
//
// Staged on purpose, so a failure tells you *which* layer broke:
//   Stage 1  serial + LED + I2C scan   -> is the board alive, is GT911 on the bus
//   Stage 2  MIPI-DSI panel + backlight -> does the EK79007 light up            (TODO)
//   Stage 3  LVGL 9 + GT911 indev       -> does it render, and how fast          (TODO)
//
// Run:  pio run -e jukebox-bringup -t upload && python3 tools/readser.py /dev/ttyACM0 30
#ifdef JUKEBOX_BRINGUP

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"

// --- Stage 1 ------------------------------------------------------------------

static void i2cScan() {
  Serial.println("[i2c] scanning bus (SDA=" + String(PIN_I2C_SDA) + " SCL=" + String(PIN_I2C_SCL) + ")…");
  int found = 0;
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      found++;
      const char *who = "";
      if (addr == GT911_ADDR_LOW || addr == GT911_ADDR_HIGH) who = "  <- GT911 touch";
      Serial.printf("[i2c]   0x%02X%s\n", addr, who);
    }
  }
  if (!found) {
    Serial.println("[i2c]   nothing found. Check that the touch reset sequence ran (GT911 holds");
    Serial.println("[i2c]   its address selection off INT level during reset) and that the panel");
    Serial.println("[i2c]   FPC is seated.");
  }
}

// The GT911 latches its I2C address from the INT pin level as RESET is released:
// INT low -> 0x5D, INT high -> 0x14. Drive the sequence explicitly so the address is
// known rather than whatever the previous boot left behind.
static void gt911Reset(bool addrHigh) {
  pinMode(PIN_TOUCH_INT, OUTPUT);
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  digitalWrite(PIN_TOUCH_INT, addrHigh ? HIGH : LOW);
  delay(10);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(10);
  // Hand INT back as an input so the controller can raise it on a touch.
  pinMode(PIN_TOUCH_INT, INPUT);
  delay(50);
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);

  Serial.println();
  Serial.println("=== sonos-jukebox / CrowPanel Advance 7\" ESP32-P4 — screen bring-up ===");
  Serial.printf("[sys] chip=%s cores=%d rev=%d cpu=%lu MHz\n",
                ESP.getChipModel(), ESP.getChipCores(), ESP.getChipRevision(),
                (unsigned long)getCpuFrequencyMhz());
  Serial.printf("[sys] flash=%lu KB  psram=%lu KB free / %lu KB total\n",
                (unsigned long)(ESP.getFlashChipSize() / 1024),
                (unsigned long)(ESP.getFreePsram() / 1024),
                (unsigned long)(ESP.getPsramSize() / 1024));
  // Internal SRAM is the resource that has repeatedly bitten the S3 units. The P4 has
  // 768 KB L2MEM, so this number should be a lot healthier here — log it to find out.
  Serial.printf("[sys] internal heap free=%lu KB  largest block=%lu KB\n",
                (unsigned long)(ESP.getFreeHeap() / 1024),
                (unsigned long)(ESP.getMaxAllocHeap() / 1024));

  pinMode(PIN_LED, OUTPUT);

  // Backlight off until the panel is initialised, so a half-configured DSI bus doesn't
  // show as a bright grey rectangle.
  pinMode(PIN_LCD_BLIGHT, OUTPUT);
  digitalWrite(PIN_LCD_BLIGHT, LOW);

  gt911Reset(false);                       // select 0x5D, Elecrow's default
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  i2cScan();

  Serial.println("[stage1] ok — LED should be blinking.");
  Serial.println("[stage2] MIPI-DSI panel bring-up not written yet (EK79007, 1024x600, 2 lanes).");
}

void loop() {
  digitalWrite(PIN_LED, HIGH);
  delay(500);
  digitalWrite(PIN_LED, LOW);
  delay(500);
}

#endif  // JUKEBOX_BRINGUP
