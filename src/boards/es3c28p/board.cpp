// ES3C28P board — implements the board HAL (core/board.h). STUB.
//
// Real bring-up is deferred: ILI9341V (4-wire SPI, 240x320) + FT6336G touch (I2C 0x38) +
// microSD + MEMS mic + RGB-LED (IO42). For now boardInit() brings up just enough LVGL — a
// headless display with a throwaway flush — so the sleep_machine unit compiles, links, and
// its screens can be built. Nothing reaches a physical panel yet.
#include "core/board.h"
#include "pins.h"
#include <Arduino.h>
#include <lvgl.h>

// Small partial draw buffer (RGB565 => 2 bytes/px), matching the nest's ~1/16-screen size.
static uint8_t s_buf[LCD_WIDTH * 20 * 2];

static void flushCb(lv_display_t *disp, const lv_area_t * /*area*/, uint8_t * /*px*/) {
  // TODO: push pixels to the ILI9341 over SPI (Arduino_GFX Arduino_ESP32SPI +
  // Arduino_ILI9341, available on the pinned Arduino_GFX v1.3.1).
  lv_display_flush_ready(disp);
}

static uint32_t tickCb() { return millis(); }

bool boardInit() {
  lv_init();
  lv_tick_set_cb(tickCb);
  lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_buffers(disp, s_buf, nullptr, sizeof(s_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  // TODO: Wire.begin() + FT6336G touch as an LVGL pointer indev; backlight; SD; mic; LED.
  Serial.println("[board] es3c28p STUB — display/touch not yet implemented");
  return true;
}

void backlightSet(uint8_t /*pct*/) { /* TODO: LEDC PWM on the ES3C28P backlight pin */ }

// No rotary encoder / knob on this board — the touch-first UX drives everything.
int32_t   encoderDelta() { return 0; }
KnobEvent knobEvent()    { return KnobEvent::None; }
bool      knobPressed()  { return false; }
bool      knobDown()     { return false; }
