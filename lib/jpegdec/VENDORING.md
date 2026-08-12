# Vendored libjpeg decoder (`lib/jpegdec`)

A **decoder-only subset of [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) 2.1.5.1**,
packaged as a PlatformIO library. It exists to decode **progressive** JPEGs, which
`TJpg_Decoder` — the fast path everywhere else in this firmware — cannot parse at all.

License: **IJG** (see `LICENSE.md` and `README.ijg`, both upstream, unmodified).

## Why it is here

Sonos serves Amazon Prime Station artwork as progressive JPEG. `TJpgDec` rejects those at the
header (`getJpgSize` fails), so the affected covers were silently dropped —
[issue #16](https://github.com/wjduenow/sonos-nest/issues/16). Measured across 108 art URLs pulled
off this household's speakers: every one of 94 Amazon Music (`sid=284`) covers is baseline, as are
Spotify and TuneIn, while `sid=201` — the whole Radio feature — is roughly half progressive.

The ESP32-P4's **hardware** JPEG decoder is baseline-only too, as is Espressif's `esp_new_jpeg`, so
there was no cheaper route on this silicon. `getaa` offers no rendition control either: `s=0/2/3/4`,
`&v=`, `&size=` all return byte-identical progressive data.

## Why 2.1.5.1 and not 3.x

3.0.4's `jdmaster.c` references the 12-bit, 16-bit and lossless decompressors **unconditionally**,
so a decoder subset does not link without either compiling all of them (roughly tripling the flash
cost for precisions no Sonos speaker will ever serve) or patching upstream source. 2.1.5.1 is the
last release before that dispatch and links cleanly from 27 files with **no patches to logic**.

## What was changed vs upstream

Exactly one thing, in one file:

- **`jmemnobs.c` allocates from PSRAM** (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`) instead of
  `malloc()`, under `#ifdef ESP_PLATFORM`. This is load-bearing, not tuning — see the comment in
  the file. Progressive decode holds the entire coefficient array before emitting a pixel: measured
  **762 KB peak with a 480 KB single block** for a 488x488 cover, against a jukebox internal heap of
  ~97 KB free / ~32 KB largest block. Stock `malloc()` fails on the first large request.

Everything else is byte-for-byte upstream. `jconfig.h` / `jconfigint.h` / `jversion.h` are the
CMake-generated ones (`-DWITH_SIMD=0 -DWITH_ARITH_DEC=0 -DWITH_ARITH_ENC=0`), with
**`SIZEOF_SIZE_T` corrected from 8 to 4** — they were generated on a 64-bit host and the targets are
32-bit.

## Measured cost

RISC-V `-Os`, `rv32imafc`, all 27 objects, before `--gc-sections` drops the unreferenced ones:

| | |
|---|---|
| flash (`.text`) | **78 KB** (app slot has ~4.3 MB free) |
| PSRAM, 300x298 cover | 305 KB peak / 180 KB largest block / 13 allocations |
| PSRAM, 488x488 cover | 762 KB peak / 480 KB largest block / 13 allocations |
| internal SRAM | none, given the `jmemnobs.c` patch |

> ⚠️ **`scale_denom` buys time, never memory.** Decoding the 488x488 at 1/2 moved the peak from
> 762 KB to 745 KB — the coefficient array is allocated at full source resolution no matter how
> small the output is. So `ART_MAX_PX` does **not** bound this, and the guard against a huge cover
> has to be on the *source* dimensions. `core/ui/jpeg_decode.cpp` caps them at `JPEG_PROG_MAX_PX`.

The flip side is a real quality win: unlike TJpgDec's powers of two, libjpeg scales by any *n*/8,
so the fallback path hits the size cap closely instead of undershooting it (a 500 px cover under a
320 px cap decodes at 312 px here, where TJpgDec would give 250).

## Re-vendoring

```bash
curl -LO https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/2.1.5.1/libjpeg-turbo-2.1.5.1.tar.gz
tar xzf libjpeg-turbo-2.1.5.1.tar.gz && cd libjpeg-turbo-2.1.5.1
cmake -B build -DWITH_SIMD=0 -DENABLE_SHARED=0 -DWITH_ARITH_DEC=0 -DWITH_ARITH_ENC=0
```

Copy the 27 `.c` files named in `library.json`'s `srcFilter`, plus the headers and the `.c`
fragments they `#include` (`jdcolext.c`, `jdcol565.c`, `jdmrgext.c`, `jdmrg565.c`, `jstdhuff.c` —
these are textually included and must NOT be added to `srcFilter`), plus the three generated
config headers from `build/`. Then re-apply the `jmemnobs.c` patch and fix `SIZEOF_SIZE_T`.
