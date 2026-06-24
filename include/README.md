# `include/` — project headers on the global include path

PlatformIO adds this directory to the compiler include path (also forced via
`-I include` in `platformio.ini`).

## Required: `lv_conf.h`

LVGL needs a config header. It is **not** committed yet. After the first
`pio run` fetches the LVGL library, copy its template and enable what you need:

```
cp .pio/libdeps/crowpanel-rotary/lvgl/lv_conf_template.h include/lv_conf.h
```

Then edit `include/lv_conf.h`:
- Set the first `#if 0` guard to `#if 1` to enable the file.
- `LV_COLOR_DEPTH 16` (RGB565 to match the ST7701 framebuffer).
- Enable PSRAM-backed buffers / custom malloc as needed.
- Enable the widgets used (arc, list, image, label).

We build with `-DLV_CONF_INCLUDE_SIMPLE`, so LVGL resolves `#include "lv_conf.h"`
from this directory.

## `secrets.h`

Copy `secrets.example.h` to `secrets.h` (gitignored) for local WiFi/Sonos config.
