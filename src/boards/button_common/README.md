# `boards/button_common/` — the parts both sonos-button boards share

Support code for the **headless button units**, used by two boards:

| board | unit env | MCU |
|---|---|---|
| `boards/esp32s3cam/` | `sleep-button` | nulllab/emakefun ESP32-S3-CAM |
| `boards/xiao_esp32s3/` | `button-v2` | Seeed XIAO ESP32S3 |

Both run the **same unit** (`units/sleep_button/`) and behave identically. They differ only in
pin map, LED polarity and physical layout — which is exactly the split `core/board.h` exists to
express, so everything above the pins lives here instead of being copied.

| file | why it is shared |
|---|---|
| `button.{h,cpp}` | the debounce + Short/Double/Triple/Long classifier. `MULTI_GAP_MS` and friends are properties of the **switch** (a FILN FLM12-FJ-6 on both units), not of the board, and the multi-press state machine is subtle enough that two copies would drift. `buttonInit(pin)` takes the one thing the boards disagree about. |
| `config_server.{h,cpp}` | the `:8080` config page — sockets, routing and ~13 KB of embedded HTML. Zero board coupling: it includes only `core/webconfig.h`, `WiFi.h`, `WebServer.h` and `core/net/logmirror.h`. Two copies would mean every page change lands twice or not at all. |

## Why here and not `core/`

Because `+<core/>` sweeps into **every** env. Putting `<WebServer.h>` in `core/` would drag it
into `nest`, `sleep-machine` and `sonos-jukebox`, which have no use for it — the mirror image of
the `art_cache.cpp` problem (issue #7) that `core/ui/` exists to prevent.

This directory defines no `boardInit()` and no `uiInit()`, so it does not trip the
one-board-plus-one-unit-per-env link guard that `platformio.ini` describes. An env opts in with a
single line:

```ini
+<boards/button_common/>
```

## Adding a third button board

Write `boards/<yours>/` with a `pins.h` and a `board.cpp` that calls `buttonInit(PIN_BUTTON)` and
`configServerStart()`, add `+<boards/button_common/>` to its env, and give it **its own unit id**
in `core/net/updater.cpp` and `core/webconfig.cpp`. That last step is not optional: both units are
`HEADLESS`, and a shared unit id means pull-OTA hands one board the other's binary.
