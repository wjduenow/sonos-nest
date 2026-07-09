#include "touch.h"
#include "pins.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

// FT6336 touch-data registers. Read 5 bytes from TD_STATUS (0x02):
//   [0] TD_STATUS  (low nibble = touch-point count)
//   [1] P1_XH      (bits[3:0] = X[11:8]; bits[7:6] = event flag)
//   [2] P1_XL      (X[7:0])
//   [3] P1_YH      (bits[3:0] = Y[11:8]; bits[7:4] = touch id)
//   [4] P1_YL      (Y[7:0])
static const uint8_t FT6336_REG_TDSTATUS = 0x02;

static bool readRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(reg);
  // FT6336 wants a STOP between the register-pointer write and the read (the nest's CST816
  // uses a repeated-start instead). NOTE: the bus must run at 100 kHz — see boardInit();
  // at 400 kHz these reads silently return all-zeros on this board.
  if (Wire.endTransmission(true) != 0) return false;
  uint8_t got = Wire.requestFrom((int)TOUCH_ADDR, (int)len);
  for (uint8_t i = 0; i < got && i < len; ++i) buf[i] = Wire.read();
  return got == len;
}

bool touchRead(uint16_t &x, uint16_t &y) {
  uint8_t d[5];
  if (!readRegs(FT6336_REG_TDSTATUS, d, sizeof(d))) return false;
  if ((d[0] & 0x0F) == 0) return false;  // no finger down
  x = ((uint16_t)(d[1] & 0x0F) << 8) | d[2];
  y = ((uint16_t)(d[3] & 0x0F) << 8) | d[4];
  // Panel runs at rotation 0 (native 240x320), so the FT6336's coordinates map 1:1 —
  // verified on hardware (center press -> ~120,160). If a future rotation change makes taps
  // land mirrored/rotated, transform here to match (e.g. y = (LCD_HEIGHT-1)-y).
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
  // Wire is already begun (at 100 kHz) by boardInit(). Hardware-reset the controller (RST
  // active-low on GPIO18), then let it fully boot before the first read. INT (GPIO17) is
  // left unused — we poll I2C in the LVGL read callback, same as the nest board.
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(20);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(300);   // FT6336 boot time before registers are valid

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, lvglReadCb);
}
