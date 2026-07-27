// See touch.h. Hand-written rather than pulling espressif/esp_lcd_touch_gt911: after the EK79007
// experience (lib/esp_lcd_ek79007/VENDORING.md) another managed component is more friction than
// ~60 lines of I2C, and the protocol is trivial.
#include "touch.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "pins.h"

// GT911 registers are 16-bit, big-endian on the wire.
static const uint16_t REG_STATUS = 0x814E;   // bit7 = data ready, low nibble = touch count
static const uint16_t REG_POINT1 = 0x8150;

static bool readReg(uint16_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(GT911_ADDR_LOW);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;    // repeated start
  size_t got = Wire.requestFrom((int)GT911_ADDR_LOW, (int)len);
  for (size_t i = 0; i < got && i < len; i++) buf[i] = Wire.read();
  return got == len;
}

static void writeReg(uint16_t reg, uint8_t val) {
  Wire.beginTransmission(GT911_ADDR_LOW);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(val);
  Wire.endTransmission();
}

// The GT911 latches its I2C address from the INT pin level as RESET is released: INT low -> 0x5D,
// INT high -> 0x14. Drive the sequence explicitly so the address is known rather than whatever
// the previous boot happened to leave behind.
static void gt911Reset() {
  pinMode(PIN_TOUCH_INT, OUTPUT);
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  digitalWrite(PIN_TOUCH_INT, LOW);          // select 0x5D
  delay(10);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(10);
  pinMode(PIN_TOUCH_INT, INPUT);             // hand INT back so the controller can raise it
  delay(50);
}

static int32_t s_x = 0, s_y = 0;
static bool    s_down = false;

static void readCb(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  uint8_t status = 0;
  if (readReg(REG_STATUS, &status, 1) && (status & 0x80)) {
    if ((status & 0x0F) > 0) {
      uint8_t p[8];
      if (readReg(REG_POINT1, p, sizeof(p))) {
        // Coordinates start at p[0]. The widely-quoted GT911 map puts a track-ID byte first with
        // x_lo at p[1]; this controller does NOT. Decoding with that offset yields values like
        // 0x5003 — an x-high byte where the low byte was expected — and coordinates in the tens
        // of thousands. Verified against a bottom-right press: raw C1 03 46 02 -> (961, 582) on
        // a 1024x600 panel, which also confirms no axis swap and no inversion is needed.
        s_x = (int32_t)(p[0] | (p[1] << 8));
        s_y = (int32_t)(p[2] | (p[3] << 8));
        s_down = true;
      }
    } else {
      s_down = false;
    }
    writeReg(REG_STATUS, 0);    // MUST clear, or the controller stops reporting
  }
  data->point.x = s_x;
  data->point.y = s_y;
  data->state = s_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

bool touchInit() {
  gt911Reset();

  uint8_t probe = 0;
  if (!readReg(REG_STATUS, &probe, 1)) {
    Serial.println("[touch ] GT911 not answering at 0x5D — FPC seated? reset sequence ran?");
    return false;
  }

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, readCb);
  Serial.println("[touch ] GT911 up, LVGL pointer indev registered");
  return true;
}
