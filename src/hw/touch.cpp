#include "touch.h"
#include "pcf8574.h"
#include "../board_pins.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

// CST816 touch-data registers (read 6 bytes from 0x01):
//   [0] gesture  [1] finger count  [2] XH(low nibble) [3] XL  [4] YH(low nibble) [5] YL
static const uint8_t CST816_REG_DATA = 0x01;

static bool readRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  uint8_t got = Wire.requestFrom((int)TOUCH_ADDR, (int)len);
  for (uint8_t i = 0; i < got && i < len; ++i) buf[i] = Wire.read();
  return got == len;
}

bool touchRead(uint16_t &x, uint16_t &y) {
  uint8_t d[6];
  if (!readRegs(CST816_REG_DATA, d, sizeof(d))) return false;
  if ((d[1] & 0x0F) == 0) return false;  // no finger
  x = ((uint16_t)(d[2] & 0x0F) << 8) | d[3];
  y = ((uint16_t)(d[4] & 0x0F) << 8) | d[5];
  return true;
}

static void lvglReadCb(lv_indev_t *, lv_indev_data_t *data) {
  uint16_t x, y;
  if (touchRead(x, y)) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void touchInit() {
  // Wire is already begun by pcf8574Init(); reset the controller via the expander.
  pcfTouchReset();

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, lvglReadCb);
}
