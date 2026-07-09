// ES3C28P display driver — ILI9341V (240x320) over 4-wire SPI, feeding LVGL 9.
//
// Structurally the twin of the nest's crowpanel_rotary/display.cpp, but this panel is a
// plain SPI TFT instead of an RGB-parallel panel, so it uses Arduino_ESP32SPI +
// Arduino_ILI9341 (both shipped by the pinned Arduino_GFX 1.3.1). Reset shares the chip
// reset (RST = -1), backlight is a plain GPIO we PWM via LEDC, and the panel wants color
// inversion on (LCD_INVERT_COLORS -> the ILI9341 "ips" flag).
#include "display.h"
#include "pins.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

// Backlight via LEDC PWM (GPIO45, active-high). The LEDC API changed between Arduino-ESP32
// core 2.x and 3.x, so wrap it to build on either — same shim the nest board uses.
static const uint32_t kBacklightFreq = 5000;   // Hz
static const uint8_t  kBacklightBits = 8;       // 0-255 duty
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  static inline void blAttach() { ledcAttach(PIN_BACKLIGHT, kBacklightFreq, kBacklightBits); }
  static inline void blWrite(uint32_t duty) { ledcWrite(PIN_BACKLIGHT, duty); }
#else
  static const uint8_t kBacklightCh = 0;
  static inline void blAttach() {
    ledcSetup(kBacklightCh, kBacklightFreq, kBacklightBits);
    ledcAttachPin(PIN_BACKLIGHT, kBacklightCh);
  }
  static inline void blWrite(uint32_t duty) { ledcWrite(kBacklightCh, duty); }
#endif

// --- Arduino_GFX: SPI data bus + ILI9341 panel, pins from pins.h ---
static Arduino_DataBus *s_bus = nullptr;
static Arduino_GFX     *s_gfx = nullptr;

// Logical (rotated) resolution. At DISPLAY_ROTATION 1/3 the panel is landscape, so the LVGL
// display and draw buffers are sized 320x240, not the native 240x320.
#if (DISPLAY_ROTATION & 1)
  static const uint16_t DISP_W = LCD_HEIGHT, DISP_H = LCD_WIDTH;   // landscape
#else
  static const uint16_t DISP_W = LCD_WIDTH,  DISP_H = LCD_HEIGHT;  // portrait
#endif

// --- LVGL draw buffers (partial; ~1/10 screen each, double-buffered) ---
// RGB565 => 2 bytes/px. In LVGL 9 sizeof(lv_color_t)!=2, so size in raw bytes.
static const uint32_t kBufLines = DISP_H / 10;
static const uint32_t kBufBytes = DISP_W * kBufLines * (LV_COLOR_DEPTH / 8);
static uint8_t *s_buf1 = nullptr;
static uint8_t *s_buf2 = nullptr;
static lv_display_t *s_disp = nullptr;

static void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  // LVGL gives native-order RGB565; draw16bitRGBBitmap re-transmits each pixel MSB-first,
  // which is what the ILI9341 expects — so no swap on a little-endian ESP32. If colors come
  // out byte-swapped on the panel, enable the swap below (or set it in lv_conf.h).
  // lv_draw_sw_rgb565_swap(px, w * h);
  s_gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px, w, h);
  lv_display_flush_ready(disp);
}

static uint32_t tickCb() { return millis(); }

bool displayInit() {
  // 1. Backlight PWM up first, so a successful panel init is immediately visible.
  blAttach();
  backlightSet(100);

  // 2. Build the SPI bus + ILI9341 panel. DC/CS/SCK/MOSI/MISO from pins.h; reset shares the
  //    chip reset (PIN_LCD_RST == -1 == GFX_NOT_DEFINED). Rotation 0 = portrait 240x320.
  //    Final arg is the ILI9341 "ips"/invert flag — this board needs color inversion on.
  s_bus = new Arduino_ESP32SPI(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI, PIN_LCD_MISO);
  s_gfx = new Arduino_ILI9341(s_bus, PIN_LCD_RST, DISPLAY_ROTATION, LCD_INVERT_COLORS /* ips/invert */);
  s_gfx->begin();          // Arduino_GFX::begin() returns void in 1.3.1 (can't check it)
  s_gfx->fillScreen(BLACK);

  // 3. LVGL: tick source, display, partial draw buffers in internal RAM (faster than PSRAM
  //    for LVGL rendering; the tight resource is SRAM but two ~1/10-screen buffers fit).
  lv_init();
  lv_tick_set_cb(tickCb);

  s_buf1 = (uint8_t *)heap_caps_malloc(kBufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s_buf2 = (uint8_t *)heap_caps_malloc(kBufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_buf1 || !s_buf2) {
    Serial.println("[display] LVGL draw buffer alloc failed");
    return false;
  }

  s_disp = lv_display_create(DISP_W, DISP_H);
  lv_display_set_flush_cb(s_disp, flushCb);
  lv_display_set_buffers(s_disp, s_buf1, s_buf2, kBufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  return true;
}

void backlightSet(uint8_t pct) {
  if (pct > 100) pct = 100;
  blWrite((uint32_t)pct * 255 / 100);
}

void displayTestPattern() {
  if (!s_gfx) return;
  int16_t hw = s_gfx->width() / 2, hh = s_gfx->height() / 2;
  s_gfx->fillRect(0,  0,  hw, hh, RED);
  s_gfx->fillRect(hw, 0,  hw, hh, GREEN);
  s_gfx->fillRect(0,  hh, hw, hh, BLUE);
  s_gfx->fillRect(hw, hh, hw, hh, WHITE);
}
