# `src/core/ui/` — the graphics-coupled half of core

Everything else in `src/core/` is device-agnostic *and* display-agnostic: it compiles with no
LVGL, no Arduino_GFX and no TJpg_Decoder anywhere in `lib_deps`. **The files in here are not.**
They include `<lvgl.h>` and/or `<TJpg_Decoder.h>`, and they hand LVGL types (`lv_image_dsc_t`)
back across their API.

That distinction exists because of one env. **`sleep-button` is headless** — no screen at all —
so its `lib_deps` is overridden to just ArduinoJson and LVGL is genuinely absent from the build.
Its source filter is:

```ini
+<core/>
-<core/ui/>
```

`+<core/>` recurses, so a new file dropped loose in `core/` is swept into that build and fails to
compile the moment it touches LVGL. A new file dropped in **here** is not.

## The rule

> **If a `core/` file includes `<lvgl.h>` or `<TJpg_Decoder.h>`, or exposes an `lv_*` type in its
> header, it belongs in `core/ui/`.**

Do not answer a headless build break by adding a per-file `-<core/some_file.cpp>` line. That list
is what this directory replaced: it has to be maintained by hand, it only ever breaks the one env
nobody builds by habit, and it stayed broken for weeks that way once already
([issue #7](https://github.com/wjduenow/sonos-nest/issues/7) — `art_cache.cpp`). Move the file.

## Consuming these from device-agnostic code

`core/app.cpp` and `core/webconfig.cpp` *do* call in here, and they are compiled into the headless
build. They guard both the include and every call site with `#ifndef HEADLESS` — the flag the
`sleep-button` env already sets. Follow that pattern rather than adding new unconditional callers:
the exclusion removes the `.cpp`, so an unguarded call is a link error, not a compile error, which
is a good deal harder to read.

Units are exempt — a unit that draws things is by definition in a build that has LVGL, and it
includes these as `core/ui/album_art.h`.

## Current members

| file | coupling |
|---|---|
| `album_art.{h,cpp}` | `<lvgl.h>`, `<TJpg_Decoder.h>` — fetch + decode the now-playing cover, expose it as an LVGL image. Also owns the `jpegLock()`/`jpegUnlock()` mutex around the TJpgDec singleton. |
| `art_cache.{h,cpp}` | `<lvgl.h>`, `<TJpg_Decoder.h>` — the jukebox station/favourite tile cache (bounded PSRAM slot ring + SD disk cache). Takes `jpegLock()` from `album_art.h`. |

## Backstop

CI (`.github/workflows/firmware.yml`) builds **every app env, including `sleep-button`, on every
push and pull request**. If this convention is broken anyway, that is what catches it.
