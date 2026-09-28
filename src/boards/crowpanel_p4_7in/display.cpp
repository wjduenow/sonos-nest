// See display.h. Lifted from the staged bring-up in display_test.cpp once each layer was proven
// on hardware; that file remains as the `jukebox-bringup` env for diagnosing a dead panel.
#include "display.h"
#include "core/net/logmirror.h"

#include <Arduino.h>
#include <lvgl.h>

#include "esp_cache.h"
#include "esp_lcd_ek79007.h"     // vendored — see lib/esp_lcd_ek79007/VENDORING.md
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "core/board.h"          // backlightSet() is part of the HAL contract; implemented here
#include "pins.h"

static esp_lcd_panel_handle_t s_panel = nullptr;
static uint8_t               *s_fb[2] = {nullptr, nullptr};
static uint8_t               *s_front = nullptr;   // the buffer the DSI DMA is scanning out
static SemaphoreHandle_t      s_frameDone = nullptr;

uint8_t *displayFrameBuffer() { return s_front; }

void backlightSet(uint8_t pct) {
  if (pct > 100) pct = 100;
  ledcWrite(PIN_LCD_BLIGHT, (uint32_t)pct * 255 / 100);
}

static uint32_t lvglTickCb() { return millis(); }

// ISR: the DMA has finished scanning out one frame and has already restarted on whichever buffer
// draw_bitmap() last selected. From here on the other buffer is no longer being read.
static bool IRAM_ATTR frameDoneCb(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t *,
                                  void *) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(s_frameDone, &woken);
  return woken == pdTRUE;
}

// Frame-rate readout. Logged only while something is animating (>= 10 frames in a 2 s window), so
// a clock ticking once a second stays quiet. `wait` is time spent blocked on the buffer flip;
// the rest of each frame period is LVGL rendering.
static uint32_t s_statStart = 0, s_statFrames = 0, s_statWaitUs = 0;

static void frameStat(uint32_t waitUs) {
  const uint32_t now = millis();
  if (s_statFrames == 0) s_statStart = now;
  s_statFrames++;
  s_statWaitUs += waitUs;
  const uint32_t span = now - s_statStart;
  if (span < 2000) return;
  if (s_statFrames >= 10) {
    LOG.printf("[display] %lu fps  (%lu frames / %lu ms, flip wait avg %lu us)\n",
               (unsigned long)(s_statFrames * 1000 / span), (unsigned long)s_statFrames,
               (unsigned long)span, (unsigned long)(s_statWaitUs / s_statFrames));
  }
  s_statFrames = 0;
  s_statWaitUs = 0;
}

// Row span helpers: every area this frame touched, as one [y1, y2] band.
struct RowSpan { int32_t y1 = INT32_MAX, y2 = -1; };
static RowSpan s_curSpan, s_prevSpan;

static void spanAdd(RowSpan &s, int32_t y1, int32_t y2) {
  if (y1 < s.y1) s.y1 = y1;
  if (y2 > s.y2) s.y2 = y2;
}

// Double-buffered DIRECT mode. LVGL renders into the back buffer (`px` is its base) while the DSI
// DMA scans out the front one, so the panel never shows a half-drawn frame — with a single buffer
// it did, and scrolling a list (every row repainted every frame) flickered heavily.
//
// Per area we only write the dirty rows back out of the CPU cache. On the LAST area of a frame we
// hand the back buffer to draw_bitmap(), which makes it the buffer the DMA starts on at the next
// frame boundary, then block until that boundary has passed. Only then may LVGL touch the old
// front buffer, which becomes its next back buffer.
//
// *** The cache write-back is load-bearing. *** The frame buffers are in PSRAM and the DSI DMA
// reads them directly, bypassing the data cache. Without it the panel shows only the cache lines
// that happened to be evicted on their own: the image comes out shredded into vertical stripes of
// otherwise-correct colour. It looks exactly like a DSI timing or lane fault and is neither.
// (esp_lcd_dpi_panel_set_pattern() draws inside the DSI peripheral with no frame buffer, so
// "hardware bars fine, our drawing shredded" is the signature of a missing sync — see
// display_test.cpp, which keeps that pattern call for exactly this diagnosis.)
//
// And it has to cover MORE than this frame's areas. Before rendering, LVGL copies the areas it drew
// into the other buffer last frame over to this one (lv_refr.c refr_sync_areas) — with the CPU, so
// those rows sit dirty in cache too, and no flush area names them. Hence the previous frame's span.
//
// Whole rows only: a row is LCD_WIDTH*2 = 2048 B, a clean multiple of the 64-byte cache line.
static void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  const size_t rowBytes = (size_t)LCD_WIDTH * 2;
  spanAdd(s_curSpan, area->y1, area->y2);

  if (!lv_display_flush_is_last(disp)) {
    esp_cache_msync(px + (size_t)area->y1 * rowBytes,
                    (size_t)(area->y2 - area->y1 + 1) * rowBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    lv_display_flush_ready(disp);
    return;
  }

  RowSpan sync = s_curSpan;
  if (s_prevSpan.y2 >= 0) spanAdd(sync, s_prevSpan.y1, s_prevSpan.y2);
  s_prevSpan = s_curSpan;
  s_curSpan = RowSpan();

  // draw_bitmap() does its own write-back of the rows it is given, then selects this buffer.
  // A take(0) AFTER it drains any frame-done that fired before the switch; if one fired between
  // the two calls we drain the real one and wait one extra frame — slower, never wrong.
  esp_lcd_panel_draw_bitmap(s_panel, 0, sync.y1, LCD_WIDTH, sync.y2 + 1, px);
  xSemaphoreTake(s_frameDone, 0);
  const uint32_t t0 = micros();
  xSemaphoreTake(s_frameDone, pdMS_TO_TICKS(50));   // one frame is 16.7 ms; bound it anyway
  frameStat(micros() - t0);

  s_front = px;
  lv_display_flush_ready(disp);
}

bool displayInit() {
  // Backlight off until there is something to show, so a half-configured panel never appears as
  // a bright grey rectangle.
  ledcAttach(PIN_LCD_BLIGHT, 5000 /* Hz */, 8 /* bits */);
  backlightSet(0);

  // 1. The MIPI D-PHY runs off internal LDO channel 3 at 2.5 V. Miss this and the DSI bus
  //    initialises without complaint and the panel simply stays dark.
  esp_ldo_channel_handle_t phyLdo = nullptr;
  esp_ldo_channel_config_t ldoCfg = {};
  ldoCfg.chan_id = 3;
  ldoCfg.voltage_mv = 2500;
  if (esp_ldo_acquire_channel(&ldoCfg, &phyLdo) != ESP_OK) {
    LOG.println("[display] FAIL: LDO3 (MIPI D-PHY)");
    return false;
  }

  // 2. DSI bus — same values as the driver's EK79007_PANEL_BUS_DSI_2CH_CONFIG() macro, spelled
  //    out because that macro sets `.phy_clk_src = 0`, which C++ will not implicitly convert to
  //    the enum type (it is written for C and does not compile from a .cpp).
  esp_lcd_dsi_bus_handle_t bus = nullptr;
  esp_lcd_dsi_bus_config_t busCfg = {};
  busCfg.bus_id = 0;
  busCfg.num_data_lanes = 2;
  busCfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
  busCfg.lane_bit_rate_mbps = 900;
  if (esp_lcd_new_dsi_bus(&busCfg, &bus) != ESP_OK) {
    LOG.println("[display] FAIL: esp_lcd_new_dsi_bus");
    return false;
  }

  // 3. DBI control channel — how the EK79007 vendor init sequence is sent.
  esp_lcd_panel_io_handle_t io = nullptr;
  esp_lcd_dbi_io_config_t dbiCfg = EK79007_PANEL_IO_DBI_CONFIG();
  if (esp_lcd_new_panel_io_dbi(bus, &dbiCfg, &io) != ESP_OK) {
    LOG.println("[display] FAIL: esp_lcd_new_panel_io_dbi");
    return false;
  }

  // 4. DPI video stream. 1024x600 plus porches is 1354x636, ~51.7 Mpx/s at 60 Hz.
  esp_lcd_dpi_panel_config_t dpiCfg =
      EK79007_1024_600_PANEL_60HZ_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
  dpiCfg.num_fbs = 2;   // double-buffered: see flushCb. 2 x 1.2 MB of PSRAM, which has ~29 MB free.

  ek79007_vendor_config_t vendorCfg = {};
  vendorCfg.mipi_config.dsi_bus = bus;
  vendorCfg.mipi_config.dpi_config = &dpiCfg;
  vendorCfg.mipi_config.lane_num = 2;

  esp_lcd_panel_dev_config_t panelCfg = {};
  panelCfg.reset_gpio_num = PIN_LCD_RST;
  panelCfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;   // verified by eye: amber reads amber
  panelCfg.bits_per_pixel = 16;
  panelCfg.vendor_config = &vendorCfg;

  if (esp_lcd_new_panel_ek79007(io, &panelCfg, &s_panel) != ESP_OK) {
    LOG.println("[display] FAIL: esp_lcd_new_panel_ek79007");
    return false;
  }
  if (esp_lcd_panel_reset(s_panel) != ESP_OK || esp_lcd_panel_init(s_panel) != ESP_OK) {
    LOG.println("[display] FAIL: panel reset/init");
    return false;
  }
  if (esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, (void **)&s_fb[0], (void **)&s_fb[1]) !=
          ESP_OK || !s_fb[0] || !s_fb[1]) {
    LOG.println("[display] FAIL: no frame buffers");
    return false;
  }
  s_front = s_fb[0];   // the driver scans out buffer 0 first

  s_frameDone = xSemaphoreCreateBinary();
  esp_lcd_dpi_panel_event_callbacks_t cbs = {};
  cbs.on_frame_buf_complete = frameDoneCb;
  if (!s_frameDone || esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, nullptr) != ESP_OK) {
    LOG.println("[display] FAIL: frame-done callback");
    return false;
  }

  // 5. LVGL, rendering directly into that frame buffer.
  lv_init();
  lv_tick_set_cb(lvglTickCb);
  lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, flushCb);
  // LVGL starts on buf_1 as its back buffer, so it must be the one NOT being scanned out.
  lv_display_set_buffers(disp, s_fb[1], s_fb[0], (uint32_t)LCD_WIDTH * LCD_HEIGHT * 2,
                         LV_DISPLAY_RENDER_MODE_DIRECT);

  LOG.printf("[display] EK79007 %dx%d up, LVGL double-buffered direct into the DSI buffers\n",
                LCD_WIDTH, LCD_HEIGHT);
  return true;
}
