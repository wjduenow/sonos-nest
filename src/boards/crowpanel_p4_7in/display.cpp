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

#include "core/board.h"          // backlightSet() is part of the HAL contract; implemented here
#include "pins.h"

static esp_lcd_panel_handle_t s_panel = nullptr;
static uint8_t               *s_fb    = nullptr;

uint8_t *displayFrameBuffer() { return s_fb; }

void backlightSet(uint8_t pct) {
  if (pct > 100) pct = 100;
  ledcWrite(PIN_LCD_BLIGHT, (uint32_t)pct * 255 / 100);
}

static uint32_t lvglTickCb() { return millis(); }

// LVGL renders in DIRECT mode straight into the DPI frame buffer that the DSI peripheral is
// already scanning out — no second buffer, no blit. All flush has to do is push the dirty rows
// back out of the CPU cache.
static void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  (void)px;
  // *** Load-bearing. *** The frame buffer is in PSRAM and the DSI DMA reads it directly,
  // bypassing the data cache. Without this write-back the panel shows only the cache lines that
  // happened to be evicted on their own: the image comes out shredded into vertical stripes of
  // otherwise-correct colour. It looks exactly like a DSI timing or lane fault and is neither.
  // (esp_lcd_dpi_panel_set_pattern() draws inside the DSI peripheral with no frame buffer, so
  // "hardware bars fine, our drawing shredded" is the signature of a missing sync — see
  // display_test.cpp, which keeps that pattern call for exactly this diagnosis.)
  //
  // Whole rows only: a row is LCD_WIDTH*2 = 2048 B, a clean multiple of the 64-byte cache line,
  // so this stays aligned without rounding. Syncing just the dirty rows rather than the whole
  // 1200 KB buffer is what keeps the frame rate up.
  const size_t rowBytes = (size_t)LCD_WIDTH * 2;
  esp_cache_msync(s_fb + (size_t)area->y1 * rowBytes,
                  (size_t)(area->y2 - area->y1 + 1) * rowBytes,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M);
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
  if (esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, (void **)&s_fb) != ESP_OK || !s_fb) {
    LOG.println("[display] FAIL: no frame buffer");
    return false;
  }

  // 5. LVGL, rendering directly into that frame buffer.
  lv_init();
  lv_tick_set_cb(lvglTickCb);
  lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_buffers(disp, s_fb, nullptr, (uint32_t)LCD_WIDTH * LCD_HEIGHT * 2,
                         LV_DISPLAY_RENDER_MODE_DIRECT);

  LOG.printf("[display] EK79007 %dx%d up, LVGL direct into the DSI buffer\n",
                LCD_WIDTH, LCD_HEIGHT);
  return true;
}
