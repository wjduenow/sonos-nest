# Setting up a dev environment

Everything you need to go from a fresh clone to a firmware binary for any of the four units.
If you only ever build the ESP32-S3 units, the short version is: install PlatformIO, then
`tools/pio run -e nest`. The rest of this page is mostly about the one thing in this repo that
is genuinely not obvious — **it builds two different chips, and they fight over PlatformIO's
one global package directory.**

---

## 1. Prerequisites

- **Python 3.8+** and **git**.
- **PlatformIO Core**. If you don't have it:
  ```bash
  curl -fsSL -o /tmp/get-platformio.py \
    https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
  python3 /tmp/get-platformio.py
  echo 'export PATH="$PATH:$HOME/.platformio/penv/bin"' >> ~/.bashrc && . ~/.bashrc
  ```
- **Disk: ~10 GB** for the ESP32-S3 units, **~19 GB** if you also build the ESP32-P4 jukebox.
  Toolchains dominate; see [Disk](#4-disk-and-what-lives-where) below.
- On **Windows/WSL2**, USB flashing needs `usbipd` bridging — see
  [`flashing-wsl.md`](flashing-wsl.md). Building needs nothing extra.

No `include/secrets.h` is required to build. Every include site is `#if __has_include`-guarded, so
a clean clone compiles; see [Secrets](#5-secrets-optional).

## 2. Build something

**Use `tools/pio`, not `pio`, for every build.** It is a thin wrapper — same arguments, same
subcommands — that picks the right package tree before handing off. Why that matters is §3.

```bash
tools/pio run -e nest                                   # ESP32-S3, round rotary display
tools/pio run -e sleep-machine                          # ESP32-S3, nightstand sleep player
tools/pio run -e sleep-button                           # ESP32-S3, headless button
tools/pio run -e sonos-jukebox                          # ESP32-P4, 7" wall panel

tools/pio run -e nest -t upload --upload-port /dev/ttyACM0   # USB flash
```

The first `sonos-jukebox` build downloads a whole second toolchain and compiles the framework:
**20 packages, 8.5 GB, 4m42s measured.** Every build after that is incremental — 9-12 s for a
one-file change, on either chip.

Bare `pio run -e <env>` still works and still produces a correct binary — the wrapper is not
load-bearing for correctness. It just stops the two chips from evicting each other's packages, and
you will feel the difference the moment you switch back and forth.

## 3. Why `tools/pio` exists

PlatformIO stores platforms, frameworks and toolchains in **one directory per machine**
(`PLATFORMIO_CORE_DIR`, default `~/.platformio`) — shared across every clone, branch and git
worktree. It is not per-project.

This repo's two platforms require **twelve package names in common** at incompatible versions:

| package | ESP32-S3 (`espressif32@6.9.0`) | ESP32-P4 (pioarduino `55.03.311`) |
|---|---|---|
| `framework-arduinoespressif32` | 2.0.17 (`~3.20017.0`) | 3.3.11 |
| `tool-esptoolpy` | 1.40501.0 | 5.3.0 |
| `toolchain-riscv32-esp` | 8.4.0 | 14.2.0 |
| …and 9 more | | |

Only one version of each can occupy the tree. Measured on the dev machine
(2026-08-11, PlatformIO 6.1.19), with a single shared tree:

```
pio run -e nest           29.7 s   reinstalled 2 packages
pio run -e sonos-jukebox  161.6 s  reinstalled 3 packages
pio run -e nest           24.0 s   reinstalled 3 packages  <- incl. the S3 Arduino core
```

Every switch evicts and re-downloads. It **self-heals** — all three builds succeeded, there is no
manual repair step — so the cost is time and a network dependency, not breakage. But offline it
*is* breakage, and two people (or two agent sessions) building different chips at once will fight
over the tree.

`tools/pio` gives each platform its own tree:

```
~/.platformio        ESP32-S3 envs   (nest, sleep-machine, sleep-button, and their -ota/-bringup variants)
~/.platformio-p4     ESP32-P4 envs   (sonos-jukebox, jukebox-*)
```

Same machine, same day, through the wrapper. After the one-off cost of populating the second tree,
switching is free:

```
tools/pio run -e sonos-jukebox  282.0 s  20 packages   <- first build, fresh tree
tools/pio run -e nest             9.9 s   0 packages
tools/pio run -e sonos-jukebox   11.8 s   0 packages
tools/pio run -e nest             8.7 s   0 packages
```

The S3 tree stays at the stock default deliberately, so a plain PlatformIO install is already
correct for it, nothing needs migrating, and every doc that references
`~/.platformio/packages/framework-arduinoespressif32/...` for an S3 env stays true.

The mapping is derived from `platformio.ini` — the wrapper resolves an env's `extends` chain, reads
its effective `platform`, and matches on it. **Adding a new `jukebox-*` env needs no change to the
wrapper.** To see where an env will build:

```bash
tools/pio --print-core-dir -e sonos-jukebox     # -> /home/you/.platformio-p4
SONOS_PIO_VERBOSE=1 tools/pio run -e nest       # announces the tree, then builds
```

Asking for two different chips in one command is refused rather than silently half-wrong:
`tools/pio run -e nest -e sonos-jukebox` exits 2 and tells you to split it.

### Two traps worth knowing

- **`pio pkg list` mutates the package tree.** It reads like a query and is not one — running it
  once per env here pruned five packages, including two that *both* platforms need, so the next
  build had to reinstall them. If you want to inspect resolution, expect a rebuild after.
- **A directory listing does not predict churn.** Both variants of a clashing package are often
  present at once — a registry-sourced `name` beside a URL-sourced `name@src-<md5>` — and
  PlatformIO evicts one anyway. Only running the build tells you.

## 4. Disk, and what lives where

Measured on the dev machine, 2026-08-11:

| path | size | what |
|---|---|---|
| `~/.platformio` | ~10 GB | S3 platform, Arduino 2.0.17, xtensa toolchain, PlatformIO itself (`penv/`) |
| `~/.platformio-p4` | 8.5 GB | P4 platform, Arduino 3.3.11, IDF 5.5.5, riscv toolchain |
| `~/.platformio-build-cache` | 927 MB | shared compiled-object cache (`build_cache_dir` in `platformio.ini`) |
| `<repo>/.pio` | ~950 MB for 3 envs | per-checkout build output; safe to delete anytime |

> **`~/.platformio` is the machine-wide default, so any *other* PlatformIO project you have also
> lives in it — and can contend with these envs exactly as the jukebox used to.** On this dev
> machine `~/Projects/rivian-status` is unpinned (`platform = espressif32`), which currently
> resolves to the **pioarduino fork at 55.03.38 / Arduino 3.3.8** on an ESP32-S3 board. Its
> packages account for roughly 5 GB of the 10 GB above, and because it wants a different
> `framework-arduinoespressif32` than `nest` does, building the two alternately re-triggers the
> eviction this page describes. If you hit that, the same fix applies: give it its own
> `PLATFORMIO_CORE_DIR`, or pin it.

The build cache is a **sibling** of both trees on purpose: entries are keyed on the compiler
invocation so the two toolchains cannot collide, and one cache serves every worktree. Putting it
inside a tree would make it half-invisible and it would be deleted along with that tree.

`.pio/` is per-worktree, so a fresh worktree recompiles from scratch — the shared build cache is
what makes that cheap. If you use `git worktree` heavily, this is the setting that pays for itself.

To reclaim space: delete `.pio/` in any checkout; delete `~/.platformio-build-cache` (rebuilt on
demand); delete `~/.platformio-p4` entirely if you never build the jukebox.

## 5. Secrets (optional)

```bash
cp include/secrets.example.h include/secrets.h
```

All fields are optional and the file is gitignored. `WIFI_SSID`/`WIFI_PASS` bake credentials in at
flash time; **leave them unset** and the unit raises a SoftAP captive portal (`<hostname>-setup`)
on first boot instead, which is how shipped units are meant to provision. `OTA_PASSWORD` is
required for wireless flashing. `SONOS_DEFAULT_ROOM`, `CLOCK_TZ` and `SONOS_ZONE_IP` are
conveniences.

CI deliberately builds **without** a secrets file, so released binaries always first-boot into the
portal rather than shipping with someone's SSID compiled in.

## 6. Flashing and watching a device

- **USB** — `tools/pio run -e <env> -t upload --upload-port /dev/ttyACM0`. On WSL2 see
  [`flashing-wsl.md`](flashing-wsl.md); the port number changes on every device reset.
- **Wireless** — the `/ota` skill (`.claude/skills/ota`), or `-e <unit>-ota`.
- **Serial log** — `python3 tools/readser.py /dev/ttyACM0 30` (`pio device monitor` does not work
  in non-interactive shells on WSL).
- **Network log** — the jukebox and the headless button mirror their log to TCP :2323
  (`nc <ip> 2323`). The button has no screen, so this is the only way to watch it.

## 7. CI

`.github/workflows/firmware.yml` builds **all four app envs on every PR and every push to `main`**,
and publishes a GitHub Release on a `v*` tag. Two things to know:

- Each matrix job is a fresh runner building exactly one env, and the PlatformIO cache is keyed per
  env — so CI never experiences the package contention this page is about. It uses `tools/pio`
  anyway, so the wrapper is exercised on every PR rather than only on developer machines.
- A `guard` job runs first, in seconds: it fails a PR that puts LVGL/TJpg includes outside
  `src/core/ui/` (that breaks only the headless `sleep-button` env — issue #7), and one that lets
  the unit-id ladders in `updater.cpp` and `webconfig.cpp` drift apart (that silently breaks
  pull-OTA). Read the comments in the workflow before changing either.

## 8. If a build breaks in a way that makes no sense

- **`Implicit dependency 'FreeRTOS.h' not found`** / **`esp_timer.h: No such file`** on an S3 env —
  the package tree got crossed. Confirm you are using `tools/pio`, then
  `pio pkg install -e <env>` to repair.
- **Internal compiler error / `Segmentation fault`** in Arduino_GFX or FrameworkArduino — transient
  on this project's dev machine. Re-run; build with `-j 2` if it recurs.
- **A change that could not possibly affect behaviour breaks behaviour** — try
  `tools/pio run -t clean -e <env>` before debugging the code. A corrupt incremental build has
  cost real hours here before.

More project-specific traps — and there are many, most of them hardware — are collected in
[`../CLAUDE.md`](../CLAUDE.md), which is the file to read before touching any subsystem.
