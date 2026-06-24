# `include/` — project headers on the global include path

PlatformIO adds this directory to the compiler include path (also forced via
`-I include` in `platformio.ini`).

## `lv_conf.h` — provided

A tuned `lv_conf.h` for this board (LVGL 9.x, RGB565, builtin allocator, Montserrat
14/20/28) is committed here. It only sets overrides; everything else falls back to
`lv_conf_internal.h` defaults. We build with `-DLV_CONF_INCLUDE_SIMPLE`, so LVGL resolves
`#include "lv_conf.h"` from this directory.

To regenerate from the upstream template instead (e.g. on an LVGL major bump):

```
cp .pio/libdeps/crowpanel-rotary/lvgl/lv_conf_template.h include/lv_conf.h
```

…then set the leading `#if 0` guard to `#if 1` and re-apply the overrides above.

## `secrets.h`

Copy `secrets.example.h` to `secrets.h` (gitignored) for local WiFi/Sonos config.
