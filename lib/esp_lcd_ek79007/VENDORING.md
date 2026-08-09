# esp_lcd_ek79007 — vendored

Upstream: **`espressif/esp_lcd_ek79007` v2.0.2**, from the ESP Component Registry
(<https://components.espressif.com/components/espressif/esp_lcd_ek79007>). Apache-2.0; the
upstream `license.txt` is kept alongside. Only `esp_lcd_ek79007.c` and its public header are
taken — the upstream `test_apps/` and CMake plumbing are dropped.

## Why vendored instead of a managed component

It *is* an ESP-IDF managed component, and `[jukebox_base] custom_component_add` will happily
download it. It just never gets **compiled**. pioarduino builds Arduino against a set of
**prebuilt** IDF libraries; the component manager records the dependency and fetches the source,
but nothing in that flow compiles a newly added component into the link. The failure is not
obvious from the outside:

1. Declare it in `src/idf_component.yml` by hand → silently ignored, never even downloaded
   (that manifest is generated; `custom_component_add` in platformio.ini is the real input).
2. Declare it properly → downloaded, but the header still isn't on the include path for `src/`.
3. Add the include path → compiles, then fails at link with a lone
   `undefined reference to 'esp_lcd_new_panel_ek79007'`.

Vendoring skips all three. PlatformIO's LDF compiles anything under `lib/` that gets `#include`d,
so the driver is built and linked like any other library — and because nothing in the ESP32-S3
builds includes its header, LDF leaves it out of the `nest` and `sleep-machine` links entirely.

## The one extra include path

`esp_lcd_ek79007.c` includes `esp_lcd_panel_interface.h`, which lives in the framework's
`esp_lcd/interface/` directory — and that directory is **not** on pioarduino's default include
path (only `esp_lcd/include` and `esp_lcd/dsi/include` are). `library.json` adds it. If a
framework bump moves that header, this is the line that breaks.

## The version macros

`esp_lcd_ek79007.c` logs its own version via `ESP_LCD_EK79007_VER_MAJOR/MINOR/PATCH`. Upstream
those come from `cu_pkg_define_version()` (the `cmake_utilities` component), which generates a
header at CMake time — machinery we dropped along with CMake. `library.json` defines the three
macros directly instead. **Keep them in step with `version`** when updating.

## Updating

Re-download the component and copy `esp_lcd_ek79007.c` + `include/esp_lcd_ek79007.h` over,
then bump `version` in `library.json`. Check the upstream CHANGELOG for changes to the
`EK79007_1024_600_PANEL_60HZ_CONFIG` timings — `display.cpp` depends on them.

## Note for callers (C++)

The driver's config macros are written for C. `EK79007_PANEL_BUS_DSI_2CH_CONFIG()` sets
`.phy_clk_src = 0`, which C++ will not implicitly convert to the enum type, so it does not
compile from a `.cpp`. Fill `esp_lcd_dsi_bus_config_t` in by hand instead (see
`boards/crowpanel_p4_7in/display_test.cpp`).
