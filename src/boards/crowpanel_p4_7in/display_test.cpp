// Screen bring-up for the ELECROW CrowPanel Advance 7" ESP32-P4 (env: jukebox-bringup).
//
// Deliberately standalone: no core/, no unit, no networking. The jukebox is the first
// ESP32-P4 board in this project, on a different toolchain (pioarduino / Arduino 3.3.x /
// IDF 5.5), so this file answers the only question that matters first — does the panel
// light up and does LVGL render on it — before anything else is ported.
//
// Staged on purpose, so a failure tells you *which* layer broke:
//   Stage 1  serial + LED + I2C scan   -> is the board alive, is GT911 on the bus
//   Stage 2  MIPI-DSI panel + backlight -> does the EK79007 light up
//   Stage 3  LVGL 9 + GT911 indev       -> does it render, and how fast
//
// Run:  pio run -e jukebox-bringup -t upload && python3 tools/readser.py /dev/ttyUSB0 40
#ifdef JUKEBOX_BRINGUP

#include <Arduino.h>
#include <Wire.h>

// MIPI-DSI stack. All ESP-IDF — there is no Arduino display API for DSI, and Arduino_GFX
// (pinned at 1.3.1 for the S3 boards) has nothing to offer here.
#include <lvgl.h>

#include "esp_cache.h"
#include "esp_lcd_ek79007.h"     // vendored driver — see lib/esp_lcd_ek79007/VENDORING.md
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"

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

// --- Stage 2: MIPI-DSI panel ---------------------------------------------------

static esp_lcd_panel_handle_t s_panel = nullptr;

// RGB565, which is what the DPI panel is configured for below.
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | (b >> 3));
}

static void backlightSet(uint8_t pct) {
  ledcWrite(PIN_LCD_BLIGHT, (uint32_t)pct * 255 / 100);
}

// Paint one screen straight into the DPI frame buffer using design-system colours, so the
// result is verifiable by eye rather than "something appeared". Wrong colour order shows up
// immediately: the accent band must read amber/orange, not blue.
static void drawVerificationFrame() {
  void *fb = nullptr;
  if (esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, &fb) != ESP_OK || !fb) {
    Serial.println("[stage2] could not get the frame buffer");
    return;
  }
  uint16_t *px = (uint16_t *)fb;

  const uint16_t bg     = rgb565(0x0e, 0x0f, 0x12);  // --screen-bg
  const uint16_t amber  = rgb565(0xe8, 0x89, 0x2b);  // --accent
  const uint16_t teal   = rgb565(0x2f, 0xa5, 0xa0);  // --teal
  const uint16_t coral  = rgb565(0xf0, 0x60, 0x5a);  // --coral
  const uint16_t text   = rgb565(0xf4, 0xf5, 0xf7);  // --screen-text

  for (int y = 0; y < LCD_HEIGHT; y++) {
    uint16_t *row = px + (size_t)y * LCD_WIDTH;
    for (int x = 0; x < LCD_WIDTH; x++) row[x] = bg;
  }
  // Four swatches across the top — amber, teal, coral, near-white.
  const uint16_t sw[4] = {amber, teal, coral, text};
  for (int i = 0; i < 4; i++) {
    for (int y = 60; y < 220; y++) {
      uint16_t *row = px + (size_t)y * LCD_WIDTH;
      for (int x = 60 + i * 240; x < 60 + i * 240 + 200; x++) row[x] = sw[i];
    }
  }
  // A 1px hairline and a 42%-filled progress bar, mirroring the Now Playing scrubber.
  for (int x = 60; x < LCD_WIDTH - 60; x++) px[(size_t)300 * LCD_WIDTH + x] = rgb565(0x2c, 0x30, 0x38);
  for (int y = 380; y < 392; y++) {
    uint16_t *row = px + (size_t)y * LCD_WIDTH;
    for (int x = 60; x < LCD_WIDTH - 60; x++) row[x] = rgb565(0x23, 0x26, 0x2d);
    for (int x = 60; x < 60 + (int)((LCD_WIDTH - 120) * 0.42f); x++) row[x] = amber;
  }
  // Single-pixel corner markers, to prove the full active area is addressed and not offset.
  px[0] = amber;
  px[LCD_WIDTH - 1] = amber;
  px[(size_t)(LCD_HEIGHT - 1) * LCD_WIDTH] = amber;
  px[(size_t)(LCD_HEIGHT - 1) * LCD_WIDTH + (LCD_WIDTH - 1)] = amber;

  // *** Load-bearing. *** The frame buffer lives in PSRAM and the DSI DMA reads it directly,
  // bypassing the CPU's data cache. Without this write-back the panel shows only the cache
  // lines that happened to be evicted on their own — the picture comes out shredded into
  // vertical stripes of otherwise-correct colour, over a background that never got written.
  // It looks like a DSI timing or lane fault and is neither.
  //
  // The hardware test pattern does NOT show this, because it is generated inside the DSI
  // peripheral and never touches a frame buffer — so "bars fine, our drawing broken" is the
  // signature of a missing cache sync, not of a broken panel.
  esp_cache_msync(fb, (size_t)LCD_WIDTH * LCD_HEIGHT * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

static bool stage2Panel() {
  // 1. The MIPI D-PHY runs off internal LDO channel 3 at 2.5 V. Without this the DSI bus
  //    initialises without complaint and the panel simply stays dark.
  esp_ldo_channel_handle_t phy_ldo = nullptr;
  esp_ldo_channel_config_t ldo_cfg = {};
  ldo_cfg.chan_id = 3;
  ldo_cfg.voltage_mv = 2500;
  if (esp_ldo_acquire_channel(&ldo_cfg, &phy_ldo) != ESP_OK) {
    Serial.println("[stage2] FAIL: could not acquire LDO3 for the MIPI D-PHY");
    return false;
  }
  Serial.println("[stage2] LDO3 @ 2500 mV up (MIPI D-PHY)");

  // 2. DSI bus: 2 data lanes at 900 Mbps — the same values as the driver's
  //    EK79007_PANEL_BUS_DSI_2CH_CONFIG() macro, spelled out because that macro is written for
  //    C and sets `.phy_clk_src = 0`. C++ won't implicitly convert 0 to the enum type, so using
  //    it from a .cpp fails to compile.
  esp_lcd_dsi_bus_handle_t bus = nullptr;
  esp_lcd_dsi_bus_config_t bus_cfg = {};
  bus_cfg.bus_id = 0;
  bus_cfg.num_data_lanes = 2;
  bus_cfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
  bus_cfg.lane_bit_rate_mbps = 900;
  if (esp_lcd_new_dsi_bus(&bus_cfg, &bus) != ESP_OK) {
    Serial.println("[stage2] FAIL: esp_lcd_new_dsi_bus");
    return false;
  }

  // 3. DBI control channel — this is how the EK79007 init sequence gets sent.
  esp_lcd_panel_io_handle_t io = nullptr;
  esp_lcd_dbi_io_config_t dbi_cfg = EK79007_PANEL_IO_DBI_CONFIG();
  if (esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, &io) != ESP_OK) {
    Serial.println("[stage2] FAIL: esp_lcd_new_panel_io_dbi");
    return false;
  }

  // 4. DPI video stream. 52 MHz pixel clock; 1024x600 plus porches is 1354x636 per frame,
  //    which is ~51.7 Mpx/s at 60 Hz.
  esp_lcd_dpi_panel_config_t dpi_cfg = EK79007_1024_600_PANEL_60HZ_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);

  ek79007_vendor_config_t vendor_cfg = {};
  vendor_cfg.mipi_config.dsi_bus = bus;
  vendor_cfg.mipi_config.dpi_config = &dpi_cfg;
  vendor_cfg.mipi_config.lane_num = 2;

  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = PIN_LCD_RST;
  panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_cfg.bits_per_pixel = 16;
  panel_cfg.vendor_config = &vendor_cfg;

  if (esp_lcd_new_panel_ek79007(io, &panel_cfg, &s_panel) != ESP_OK) {
    Serial.println("[stage2] FAIL: esp_lcd_new_panel_ek79007");
    return false;
  }
  if (esp_lcd_panel_reset(s_panel) != ESP_OK || esp_lcd_panel_init(s_panel) != ESP_OK) {
    Serial.println("[stage2] FAIL: panel reset/init");
    return false;
  }
  Serial.printf("[stage2] EK79007 up: %dx%d, DSI 2 lanes @900 Mbps, DPI 52 MHz\n",
                LCD_WIDTH, LCD_HEIGHT);

  // 5. Backlight on only now, so a half-configured panel never shows as a grey rectangle.
  backlightSet(100);

  // 6. Hardware test pattern first. This is generated inside the DSI peripheral and needs no
  //    frame buffer, so if the bars appear the whole DSI path and panel timing are proven even
  //    if our own drawing is broken.
  Serial.println("[stage2] colour bars (hardware pattern) for 4 s — expect vertical bars…");
  esp_lcd_dpi_panel_set_pattern(s_panel, MIPI_DSI_PATTERN_BAR_VERTICAL);
  delay(4000);

  // 7. Our own pixels.
  esp_lcd_dpi_panel_set_pattern(s_panel, MIPI_DSI_PATTERN_NONE);
  Serial.println("[stage2] drawing design-system frame…");
  Serial.println("         expect: near-black background; four swatches across the top reading");
  Serial.println("         AMBER, TEAL, CORAL, WHITE left-to-right; an amber bar 42% filled.");
  Serial.println("         If the amber swatch looks blue, the RGB element order is wrong.");
  drawVerificationFrame();
  return true;
}

// --- Stage 3: GT911 touch + LVGL 9 ---------------------------------------------

// Minimal GT911 reader. Deliberately hand-written rather than pulling
// espressif/esp_lcd_touch_gt911: after the EK79007 experience (see VENDORING.md), another
// managed component is more friction than 60 lines of I2C, and the protocol is trivial.
// Registers are 16-bit, big-endian on the wire.
// Set to 1 to print the raw 8-byte point block on every touch — the tool for settling coordinate
// questions (offset, axis order, inversion) from data instead of from a datasheet.
#define GT911_RAW_DUMP 0

static const uint16_t GT911_REG_STATUS = 0x814E;  // bit7 = data ready, low nibble = touch count
static const uint16_t GT911_REG_POINT1 = 0x8150;  // 8 bytes per point

static bool gt911Read(uint16_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(GT911_ADDR_LOW);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;      // repeated start
  size_t got = Wire.requestFrom((int)GT911_ADDR_LOW, (int)len);
  for (size_t i = 0; i < got && i < len; i++) buf[i] = Wire.read();
  return got == len;
}

static void gt911Write(uint16_t reg, uint8_t val) {
  Wire.beginTransmission(GT911_ADDR_LOW);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(val);
  Wire.endTransmission();
}

// Latest touch state, refreshed by the LVGL indev callback.
static volatile int32_t s_touchX = 0, s_touchY = 0;
static volatile bool s_touchDown = false;
// Latched so a press can be inspected long after it happened. Reading raw touch over serial is
// otherwise a synchronisation game — the capture window has to overlap the press, and an empty
// window is indistinguishable from broken touch.
static uint8_t  s_lastRaw[8] = {0};
static uint32_t s_touchCount = 0;

static void touchRead(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  uint8_t status = 0;
  if (!gt911Read(GT911_REG_STATUS, &status, 1)) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  if (status & 0x80) {                       // a fresh sample is ready
    uint8_t n = status & 0x0F;
    if (n > 0) {
      uint8_t p[8];
      if (gt911Read(GT911_REG_POINT1, p, sizeof(p))) {
        // Byte layout at 0x8150. The widely-quoted GT911 map puts a track-ID byte first
        // (x_lo at p[1]), but this panel's controller returns coordinates from p[0] — decoding
        // with the track-ID offset produced values like 0x5003, i.e. an x-high byte sitting
        // where the low byte was expected. GT911_RAW_DUMP prints both readings so the choice is
        // made from data rather than from a datasheet that doesn't match the part.
        s_touchX = (int32_t)(p[0] | (p[1] << 8));
        s_touchY = (int32_t)(p[2] | (p[3] << 8));
        s_touchDown = true;
        memcpy(s_lastRaw, p, sizeof(s_lastRaw));
        s_touchCount++;
      }
    } else {
      s_touchDown = false;
    }
    gt911Write(GT911_REG_STATUS, 0);         // MUST clear, or the controller stops reporting
  }
  data->point.x = s_touchX;
  data->point.y = s_touchY;
  data->state = s_touchDown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static uint32_t lvglTickCb() { return millis(); }

// DIRECT render mode: LVGL draws straight into the DPI frame buffer that the DSI peripheral is
// already scanning out, so there is no second buffer and no blit. The only thing flush has to do
// is write the dirty rows back out of the CPU cache — same hazard as stage 2, just per-region.
static uint8_t *s_fb = nullptr;

static volatile uint32_t s_flushes = 0;      // real renders, as opposed to loop iterations
static volatile uint32_t s_flushRows = 0;    // rows pushed, i.e. how much area is actually dirty

static void lvglFlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  (void)px;
  s_flushes++;
  s_flushRows += (uint32_t)(area->y2 - area->y1 + 1);
  // Sync whole rows: a row is 1024*2 = 2048 bytes, a clean multiple of the 64-byte cache line,
  // so this stays aligned without rounding. Syncing only the dirty rows rather than the whole
  // 1200 KB buffer is what keeps the frame rate up.
  const size_t rowBytes = (size_t)LCD_WIDTH * 2;
  uint8_t *start = s_fb + (size_t)area->y1 * rowBytes;
  size_t len = (size_t)(area->y2 - area->y1 + 1) * rowBytes;
  esp_cache_msync(start, len, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  lv_display_flush_ready(disp);
}

static lv_obj_t *s_touchLabel = nullptr;
static lv_obj_t *s_fpsLabel = nullptr;

// A deliberately design-system-flavoured screen: near-black ground, one amber accent, mono-ish
// metadata. It doubles as a first look at whether the tokens actually read well at 7".
static void buildTestUi() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0e0f12), 0);   // --screen-bg
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "Sonos Jukebox");
  lv_obj_set_style_text_color(title, lv_color_hex(0xf4f5f7), 0);  // --screen-text
  lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 40, 32);

  lv_obj_t *sub = lv_label_create(scr);
  lv_label_set_text(sub, "stage 3  .  LVGL 9 + GT911");
  lv_obj_set_style_text_color(sub, lv_color_hex(0x9aa0ab), 0);    // --screen-text-mut
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_20, 0);
  lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 40, 96);

  // Accent slab — the one saturated thing on screen, per the design system's "one accent" rule.
  lv_obj_t *accent = lv_obj_create(scr);
  lv_obj_set_size(accent, 240, 120);
  lv_obj_align(accent, LV_ALIGN_TOP_RIGHT, -40, 32);
  lv_obj_set_style_bg_color(accent, lv_color_hex(0xe8892b), 0);   // --accent
  lv_obj_set_style_border_width(accent, 0, 0);
  lv_obj_set_style_radius(accent, 20, 0);                        // --r-lg

  // Touch target: press it and it fills; release and it goes back to outline.
  lv_obj_t *btn = lv_button_create(scr);
  lv_obj_set_size(btn, 320, 110);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 40);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x191b20), 0);     // --screen-elev
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xe8892b), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(btn, lv_color_hex(0x2c3038), 0); // --screen-line
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_radius(btn, 20, 0);
  lv_obj_t *btnLabel = lv_label_create(btn);
  lv_label_set_text(btnLabel, "TOUCH ME");
  lv_obj_set_style_text_font(btnLabel, &lv_font_montserrat_24, 0);
  lv_obj_center(btnLabel);

  s_touchLabel = lv_label_create(scr);
  lv_label_set_text(s_touchLabel, "touch: --");
  lv_obj_set_style_text_color(s_touchLabel, lv_color_hex(0x9aa0ab), 0);
  lv_obj_set_style_text_font(s_touchLabel, &lv_font_montserrat_24, 0);
  lv_obj_align(s_touchLabel, LV_ALIGN_BOTTOM_LEFT, 40, -40);

  // Something must actually move, or the fps figure is meaningless: with a static screen LVGL
  // finds nothing dirty and skips rendering entirely (that path idles around 480). Sliding the
  // accent slab forces a real redraw of a representative dirty area every frame.
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, accent);
  lv_anim_set_values(&a, 0, -520);
  lv_anim_set_duration(&a, 1400);
  lv_anim_set_playback_duration(&a, 1400);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
    lv_obj_align((lv_obj_t *)obj, LV_ALIGN_TOP_RIGHT, -40 + v, 32);
  });
  lv_anim_start(&a);

  s_fpsLabel = lv_label_create(scr);
  lv_label_set_text(s_fpsLabel, "fps: --");
  lv_obj_set_style_text_color(s_fpsLabel, lv_color_hex(0x6a7079), 0);  // --screen-text-dim
  lv_obj_set_style_text_font(s_fpsLabel, &lv_font_montserrat_24, 0);
  lv_obj_align(s_fpsLabel, LV_ALIGN_BOTTOM_RIGHT, -40, -40);
}

static bool stage3Lvgl() {
  if (esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, (void **)&s_fb) != ESP_OK || !s_fb) {
    Serial.println("[stage3] FAIL: no frame buffer");
    return false;
  }

  lv_init();
  lv_tick_set_cb(lvglTickCb);

  lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, lvglFlushCb);
  lv_display_set_buffers(disp, s_fb, nullptr, (uint32_t)LCD_WIDTH * LCD_HEIGHT * 2,
                         LV_DISPLAY_RENDER_MODE_DIRECT);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchRead);

  buildTestUi();
  Serial.println("[stage3] LVGL 9 up, GT911 indev registered, DIRECT render into the DSI buffer");
  return true;
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
  ledcAttach(PIN_LCD_BLIGHT, 5000 /* Hz */, 8 /* bits */);
  backlightSet(0);

  gt911Reset(false);                       // select 0x5D, Elecrow's default
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  i2cScan();
  Serial.println("[stage1] ok — LED should be blinking.");

  if (stage2Panel()) {
    Serial.println("[stage2] ok — panel is live.");
  } else {
    Serial.println("[stage2] FAILED — see the line above for the layer that broke.");
  }
  Serial.printf("[sys] internal heap after panel init: free=%lu KB  psram free=%lu KB\n",
                (unsigned long)(ESP.getFreeHeap() / 1024),
                (unsigned long)(ESP.getFreePsram() / 1024));

  delay(3000);   // let the stage-2 frame stand for a moment before LVGL paints over it

  if (stage3Lvgl()) {
    Serial.println("[stage3] ok — touch the button; coords and fps are on screen and on serial.");
  } else {
    Serial.println("[stage3] FAILED.");
  }
  Serial.printf("[sys] internal heap after LVGL init: free=%lu KB  largest block=%lu KB\n",
                (unsigned long)(ESP.getFreeHeap() / 1024),
                (unsigned long)(ESP.getMaxAllocHeap() / 1024));
}

void loop() {
  static uint32_t frames = 0, lastReport = 0, lastBlink = 0;
  static bool led = false;

  lv_timer_handler();
  frames++;

  uint32_t now = millis();
  if (now - lastBlink >= 500) {
    lastBlink = now;
    led = !led;
    digitalWrite(PIN_LED, led);
  }
  if (now - lastReport >= 1000) {
    uint32_t fps = frames * 1000 / (now - lastReport);
    frames = 0;
    lastReport = now;

    if (s_fpsLabel) lv_label_set_text_fmt(s_fpsLabel, "loop %lu/s", (unsigned long)fps);
    if (s_touchLabel) {
      if (s_touchDown) lv_label_set_text_fmt(s_touchLabel, "touch: %ld, %ld",
                                             (long)s_touchX, (long)s_touchY);
      else lv_label_set_text(s_touchLabel, "touch: --");
    }
    // Serial too — the on-screen numbers are useless for diagnosing a panel that isn't drawing.
    // `loop` is iterations/sec — bounded by delay(2) and by LVGL's ~33 ms refresh period, so it
    // says nothing about render cost. `flush` is actual renders/sec and `rows` the area they
    // covered; those two are the numbers that matter.
    uint32_t flushes = s_flushes, rows = s_flushRows;
    s_flushes = 0; s_flushRows = 0;
    Serial.printf("[stage3] loop=%lu/s  flush=%lu/s rows=%lu  touch=%s (%ld,%ld)  heap=%lu KB\n",
                  (unsigned long)fps, (unsigned long)flushes, (unsigned long)rows,
                  s_touchDown ? "down" : "up ", (long)s_touchX, (long)s_touchY,
                  (unsigned long)(ESP.getFreeHeap() / 1024));
#if GT911_RAW_DUMP
    if (s_touchCount) {
      const uint8_t *p = s_lastRaw;
      Serial.printf("[gt911] n=%lu raw %02X %02X %02X %02X %02X %02X %02X %02X | "
                    "p0=(%ld,%ld)  p1=(%ld,%ld)\n",
                    (unsigned long)s_touchCount,
                    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                    (long)(p[0] | (p[1] << 8)), (long)(p[2] | (p[3] << 8)),
                    (long)(p[1] | (p[2] << 8)), (long)(p[3] | (p[4] << 8)));
    }
#endif
  }
  delay(2);
}

#endif  // JUKEBOX_BRINGUP
