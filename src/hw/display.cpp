#include "display.h"
#include "../board_pins.h"
#include "pcf8574.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

// Backlight via LEDC PWM. The LEDC API changed between Arduino-ESP32 core 2.x and 3.x,
// so wrap it to build on either.
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

// --- Arduino_GFX panel (RGB parallel + 3-wire SPI init bus), pins from board_pins.h ---
static Arduino_ESP32RGBPanel *s_bus = nullptr;
static Arduino_ST7701_RGBPanel *s_gfx = nullptr;

// --- LVGL draw buffers (partial; ~1/10 screen each, double-buffered) ---
// RGB565 => 2 bytes/px. NOTE: in LVGL 9 sizeof(lv_color_t)!=2, so size the buffer in
// raw bytes (LV_COLOR_DEPTH/8), not by sizeof(lv_color_t).
static const uint32_t kBufLines = LCD_HEIGHT / 10;
static const uint32_t kBufBytes = LCD_WIDTH * kBufLines * (LV_COLOR_DEPTH / 8);
static uint8_t *s_buf1 = nullptr;
static uint8_t *s_buf2 = nullptr;
static lv_display_t *s_disp = nullptr;

static void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  // LVGL gives RGB565. Arduino_GFX wants native-endian uint16_t. If colors look byte-
  // swapped on the panel, enable the swap below (or set it in lv_conf.h).
  // lv_draw_sw_rgb565_swap(px, w * h);
  s_gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px, w, h);
  lv_display_flush_ready(disp);
}

static uint32_t tickCb() { return millis(); }

bool displayInit() {
  // 1. Power the panel + run the reset sequence (both via the PCF8574 expander).
  pcfLcdPower(true);
  delay(100);
  pcfLcdReset();

  // 2. Backlight PWM up.
  blAttach();
  backlightSet(100);

  // 3. Build the RGB bus + ST7701 panel (args verbatim from Elecrow's example).
  s_bus = new Arduino_ESP32RGBPanel(
      PIN_LCD_CS, PIN_LCD_SCK, PIN_LCD_SDA,
      PIN_RGB_DE, PIN_RGB_VSYNC, PIN_RGB_HSYNC, PIN_RGB_PCLK,
      PIN_RGB_R0, PIN_RGB_R1, PIN_RGB_R2, PIN_RGB_R3, PIN_RGB_R4,
      PIN_RGB_G0, PIN_RGB_G1, PIN_RGB_G2, PIN_RGB_G3, PIN_RGB_G4, PIN_RGB_G5,
      PIN_RGB_B0, PIN_RGB_B1, PIN_RGB_B2, PIN_RGB_B3, PIN_RGB_B4);

  s_gfx = new Arduino_ST7701_RGBPanel(
      s_bus, GFX_NOT_DEFINED /* RST handled via PCF8574 */, 0 /* rotation */,
      false /* IPS */, LCD_WIDTH, LCD_HEIGHT,
      st7701_type5_init_operations, sizeof(st7701_type5_init_operations),
      true /* BGR */,
      LCD_HSYNC_FP, LCD_HSYNC_PW, LCD_HSYNC_BP,
      LCD_VSYNC_FP, LCD_VSYNC_PW, LCD_VSYNC_BP);

  s_gfx->begin();          // Arduino_ST7701_RGBPanel::begin() returns void in 1.3.1
  s_gfx->fillScreen(BLACK);

  // 4. LVGL: tick source, display, partial draw buffers (allocated below).
  lv_init();
  lv_tick_set_cb(tickCb);

  // Draw buffers in internal RAM (faster for LVGL rendering than PSRAM; the panel
  // framebuffer already lives in PSRAM inside Arduino_GFX).
  s_buf1 = (uint8_t *)heap_caps_malloc(kBufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s_buf2 = (uint8_t *)heap_caps_malloc(kBufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_buf1 || !s_buf2) {
    return false;
  }

  s_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
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
  int16_t hw = LCD_WIDTH / 2, hh = LCD_HEIGHT / 2;
  s_gfx->fillRect(0,  0,  hw, hh, RED);
  s_gfx->fillRect(hw, 0,  hw, hh, GREEN);
  s_gfx->fillRect(0,  hh, hw, hh, BLUE);
  s_gfx->fillRect(hw, hh, hw, hh, WHITE);
}
