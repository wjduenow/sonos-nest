# Scalable OTA — CI builds + device-pull updates

> Status: **Phase 1 built; Phase 2 built + hardware-verified** (2026-07-20). Phase 1 = CI
> (`.github/workflows/firmware.yml` + `tools/build_manifest.py`). Phase 2 = the device pull path
> (`src/core/net/updater.*` + settings/webconfig/registrar/app wiring). **Hardware pass done** on
> the nest: served a version-bumped build off a laptop HTTP server, set `updateUrl` + approved via
> `/api/config`, and watched fetch → detect → download → flash → reboot converge to
> `available=null`. Two bugs the test caught and fixed (both are the espota flash hazards this repo
> documents, which the pull path initially didn't honor):
> 1. **UI/art cache-contention** during flash writes → reset mid-download. Fix: `updaterActive()`
>    ORed into the uiTask/artTask backoff gates (commit 38588cb).
> 2. **Task-WDT reset** — `HTTPUpdate`'s loop never yields, starving IDLE0 → ~5 s WDT fires mid-
>    download (reset reason 6). Fix: `onProgress` `delay(1)` per chunk; also surfaced
>    `esp_reset_reason()` in the health JSON, which is what made it diagnosable without serial
>    (commit 5cc1eb7).
>
> **Phase 3 built + verified** (portal as LAN update source, `sonos-portal/`): `app/firmware.py`
> mirrors a configured repo's latest Release; `/api/firmware` serves a device-facing manifest with
> URLs rewritten to the portal and a per-device `approved` flag; `/firmware/<bin>` streams the
> cached image; dashboard gains per-tile version-diff + Update / Update-all; and the heartbeat
> returns `{"recheck":true}` while an approval is pending so a dashboard click lands within ~45 s
> (firmware `registrar.cpp` reads it → `updaterForceCheck()`). Hardware-verified: a device pulled an
> update **routed through the portal** (fetch → approve-gate holds → download from `/firmware` →
> flash → converge); the approve + heartbeat-recheck lifecycle verified by unit test (mDNS
> auto-registration isn't testable under WSL, so that leg is logic-verified only).
>
> **Per-page config UI built** (commit b6cfb76): all three boards' `:8080` pages now render an
> "Updates" card (auto-update toggle, update-source URL, running/available version, "Update now")
> wired to the existing `ota` block + `otaAuto`/`updateUrl`/`updateNow` fields. Nest verified serving
> it on hardware.
>
> **Remaining:** flash a real CI-published Release binary as the Phase 1 hardware pass (the CI build
> + Release job are verified — `workflow_dispatch` built all three envs green, and `v0.1.0` exercises
> the tag/release path); **redeploy the portal** with the Phase 3 code on the Pi/HA host + set
> `FIRMWARE_REPO` (the LAN mirror is inert until then); flash button + sleep-machine off old firmware
> (`4cdb301`) so they report OTA status. The espota push path (`*-ota` envs, `/ota` skill) stays the
> dev-iteration flow — this is a fleet path on top.

## Context

Today firmware only reaches a device one way: **espota push** from a build host. Your laptop holds
the `.bin`, resolves each device's IP, and runs `espota.py` against it — one device at a time, and
only from the machine that just built. That's the "only from here" bottleneck. Two independent
problems hide inside it:

1. **Binaries come from a laptop.** No reproducible, provenance-stamped build anyone/anything else
   can produce. `FW_VERSION` = `git describe --dirty`, so an ad-hoc build literally reports itself
   `-dirty`.
2. **Distribution is push, per-device, manual.** Nothing scales past a handful of devices, and a
   headless unit in another room needs someone to go find its IP and aim a laptop at it.

We already own most of the machinery to fix both:

- **The portal** (`sonos-portal/`, plans/05) already knows every device on the LAN — `mdnsName`,
  `ip`, `unit` (nest/sleep/button), `board`, `fwVersion`, `configUrl`, zones — via outbound
  self-registration + a 45 s heartbeat. It's an always-on Pi/HA service.
- **`FW_VERSION`** (`tools/git_version.py`, `git describe`) is already injected per-build and
  already reported to the portal. "What's running where" is already visible per tile.
- **Dual-OTA partitions** are already in place (`default_16MB.csv` on nest/sleep, `default_8MB.csv`
  on the button — both standard app0/app1/fs OTA layouts), and **`HTTPClient` is already linked**
  (album art, registrar). ESP32's `HTTPUpdate` (pull-OTA) ships *with the Arduino core* — **no new
  device dependency.**

So the missing pieces are just: (1) CI that produces the binaries, and (2) a **pull** path so a
device fetches its own update instead of being pushed to.

### Decisions (confirmed with user)

- **Keep the "no cloud / no server" promise intact.** That promise is about the *runtime control
  path* — a device plays music and controls Sonos with no server and no cloud in the loop, and that
  is unchanged. OTA update-checking is **opt-in and source-agnostic**: a device holds a single
  *manifest URL* setting (empty by default). What it points at decides the philosophy —

  | `updateUrl` | Meaning | Cloud? | Server you run? |
  |---|---|---|---|
  | *(empty — default)* | espota-only, exactly like today | none | none |
  | portal on the LAN | fleet updates, 100 % local | **none** | the portal (already optional) |
  | GitHub raw/release | updates with zero infra | GitHub as *build store* only | none |

  A unit shipped out of the box is pure espota — no server, no cloud. Pointed at the portal, the
  **entire** firmware-distribution path is LAN-local and GitHub never touches a device. This is
  additive; nothing here is mandatory.

- **Per-device policy, not a fleet mandate.** Each device has its own `otaAuto` (bool) +
  `updateUrl` (string), set through the same NVS + `webconfig` path as room/brightness/sleep-track,
  and surfaced on that unit's existing `:8080` config page (the nest got one in 7fe7c76, so *every*
  shipping unit now has a config surface). The behaviour matrix the user asked for:
  - **update-available is always shown** (on the device Settings screen *and* on the portal tile),
    regardless of `otaAuto`;
  - `otaAuto = true` → device self-applies;
  - `otaAuto = false` → device waits for a one-click **Approve** (from its own config page *or* the
    portal dashboard).

- **CI is worth doing on its own** (Phase 1), independent of any pull path. It just stops binaries
  coming from a laptop; espota still consumes them.

- **Signed/secure-boot OTA is out of scope.** LAN, plain HTTP, hobby fleet. Noted as a future knob
  (§ Non-goals), not built.

---

## Phase 1 — CI builds (GitHub Actions → Releases)

**Goal:** a `v*` tag produces a GitHub Release with one `firmware-<unit>.bin` per shipping env plus
a `manifest.json`. Nothing device-side changes; espota now pulls its `.bin` from a Release instead
of a laptop build dir.

### Shipping envs to build (matrix)

Only the three **app** envs — not the bring-up/test envs:

| env | unit id | board | flash | partitions |
|---|---|---|---|---|
| `nest` | `nest` | crowpanel_rotary | 16 MB | `default_16MB.csv` |
| `sleep-machine` | `sleep` | es3c28p | 16 MB | `default_16MB.csv` |
| `sleep-button` | `button` | esp32s3cam | 8 MB | `default_8MB.csv` |

The `unit` id column is exactly what `registrationJson()` reports (`webconfig.cpp:134-142`) — the
manifest is keyed on it so a device looks up its own entry with a string it already knows.

### Workflow sketch — `.github/workflows/firmware.yml`

```yaml
name: firmware
on:
  push:
    tags: ['v*']            # a tag is the trigger; git describe == the tag → clean FW_VERSION
  workflow_dispatch: {}     # manual builds (no Release, artifacts only) for testing CI
jobs:
  build:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        include:
          - { env: nest,          unit: nest }
          - { env: sleep-machine, unit: sleep }
          - { env: sleep-button,  unit: button }
    steps:
      - uses: actions/checkout@v4
        with: { fetch-depth: 0, fetch-tags: true }   # git describe needs tags + history
      - uses: actions/setup-python@v5
        with: { python-version: '3.12' }
      - run: pip install platformio
      # NO secrets.h in CI — every include site is `#if __has_include`-guarded and
      # WIFI_SSID/WIFI_PASS are `#if defined`-guarded, so the build is clean without one AND bakes
      # no WiFi creds (Release binaries still captive-portal provision). Copying secrets.example.h
      # would hardcode "your-ssid" and disable the portal — do NOT do it.
      - run: pio run -e ${{ matrix.env }}
      - run: cp .pio/build/${{ matrix.env }}/firmware.bin firmware-${{ matrix.unit }}.bin
      - uses: actions/upload-artifact@v4
        with: { name: firmware-${{ matrix.unit }}, path: firmware-${{ matrix.unit }}.bin }
  release:
    needs: build
    if: startsWith(github.ref, 'refs/tags/')
    runs-on: ubuntu-latest
    steps:
      - uses: actions/download-artifact@v4
        with: { path: bins, merge-multiple: true }
      - name: Build manifest.json               # {version, per-unit {url,bin,sha256}}
        run: |
          VER="${GITHUB_REF_NAME}"
          BASE="https://github.com/${GITHUB_REPOSITORY}/releases/download/${VER}"
          # emit manifest keyed by unit id; url = release asset download URL
          python3 tools/build_manifest.py "$VER" "$BASE" bins > bins/manifest.json
      - uses: softprops/action-gh-release@v2
        with:
          files: |
            bins/firmware-*.bin
            bins/manifest.json
```

- **`secrets.h` in CI — omit it entirely.** Every include site is `#if __has_include("secrets.h")`
  guarded and `WIFI_SSID`/`WIFI_PASS` are `#if defined` guarded (`wifi.cpp:40,69`), so the build is
  clean with no header at all — and bakes no WiFi creds, so a public Release binary still first-boot
  provisions via the captive portal. **Do not copy `secrets.example.h`:** it `#define`s
  `WIFI_SSID "your-ssid"` unconditionally, which would disable the portal on every shipped unit.
  `OTA_PASSWORD` is irrelevant to a pull build (it's the espota listener's password).
- **Provenance.** With `fetch-depth: 0` and a tag trigger, `git describe --tags` on a clean tree
  emits the bare tag (e.g. `v0.6.0`), so a Release binary reports a clean `FW_VERSION` and a
  laptop dev build stays `v0.6.0-3-gabc-dirty`. That difference is the whole update trigger (below).
- **`tools/build_manifest.py`** (new, ~30 lines): read the three bins, sha256 each, emit the schema
  in the next section. Kept in-repo so the standalone-GitHub path (no portal) has a manifest too.
- **Transient GCC ICE** (the known local flake) is a *local* hardware fault; CI runners won't hit
  it. No `-j` clamp needed there.

### Manifest schema (`manifest.json`)

One document, all units, served verbatim from the Release (GitHub path) or mirrored by the portal:

```json
{
  "version": "v0.6.0",
  "units": {
    "nest":   { "bin": "firmware-nest.bin",   "url": "https://github.com/.../firmware-nest.bin",   "sha256": "…", "size": 1234567 },
    "sleep":  { "bin": "firmware-sleep.bin",  "url": "https://github.com/.../firmware-sleep.bin",  "sha256": "…", "size": 2345678 },
    "button": { "bin": "firmware-button.bin", "url": "https://github.com/.../firmware-button.bin", "sha256": "…", "size":  456789 }
  }
}
```

Absolute `url` per unit so a device needs only the manifest URL to find its `.bin`. `sha256`/`size`
are advisory in v1 (HTTPUpdate verifies the ESP image header itself); wire the hash check in later
if desired.

**Ship Phase 1 alone first.** At this point: tag → CI → Release; the `/ota` skill/espota just points
`-f` at a downloaded Release asset instead of `.pio/build/...`. Binaries have left the laptop.

---

## Phase 2 — Device pull path (`core/net/updater.{h,cpp}`)

**Goal:** a device fetches the manifest, decides whether to update, and self-flashes via
`HTTPUpdate` — governed entirely by its own `otaAuto` + `updateUrl`. Device-agnostic → lives in
`core/`, ships to all units (nest/sleep/button).

### New settings (`core/settings.h`)

Same shape as the existing keys (`settingsPortal` etc.):

```cpp
bool    settingsOtaAuto();               // auto-apply updates? default false
void    settingsSetOtaAuto(bool on);
String  settingsUpdateUrl();             // firmware manifest URL ("" = disabled, espota-only)
void    settingsSetUpdateUrl(const String &url);
```

Default `updateUrl` = `""` → the whole feature is dormant until someone opts in (keeps the
out-of-box no-server promise). A convenience: when the portal is known (`settingsPortal()` is set)
and `updateUrl` is empty, the updater *may* default to `http://<portal>/api/firmware` — decided by
the OPEN QUESTION below.

### The update contract (uniform across portal & GitHub)

`updater` GETs the manifest and, harmlessly to a static host, appends identity:

```
GET <updateUrl>?id=<mdnsName>&fw=<FW_VERSION>&unit=<unit>
```

- A **static GitHub** `manifest.json` ignores the query → returns the schema above.
- The **portal** reads the query and MAY add a per-device `"approved": true|false` to the unit
  entry (that's how the dashboard "Approve" button works — see Phase 3).

Decision to flash, computed on-device:

```
target   = manifest.units[<this unit>]
available = target && target.version != FW_VERSION      // clean tag vs running string
apply     = available && ( settingsOtaAuto() || target.approved == true )
```

- **`version != FW_VERSION`** is a deliberate *string* compare, not semver parsing. CI only publishes
  clean tags, so a device on the blessed release reads `v0.6.0 == v0.6.0` (no update); anything else
  (older tag, dev `-dirty` build) differs → update available. No version-math on the MCU.
- **`available` always sets a flag** (`updaterAvailable()` / the version string) shown on the device
  Settings screen and reported to the portal, *regardless of `apply`* — that's the "always show
  update-available" requirement.
- **`approved`** is the portal's per-device override for `otaAuto=false` units. Absent on a static
  manifest → only `otaAuto` governs. This is what makes dashboard approval work without a second
  push channel: the device is already polling; the portal just answers "yes, you".

### Applying — `HTTPUpdate`

```cpp
#include <HTTPUpdate.h>
WiFiClient client;                        // plain HTTP on the LAN; no TLS
httpUpdate.rebootOnUpdate(true);
t_httpUpdate_return r = httpUpdate.update(client, target.url, FW_VERSION);
```

- **Dual-OTA already configured** → `HTTPUpdate` writes the inactive slot and flips on success; a
  failed/partial download leaves the running firmware untouched (same safety as espota).
- **Do not interrupt playback.** Apply only at a safe moment: `otaAuto` self-applies **on next
  reboot** or while idle (not while `localAudioActive()` / actively streaming / OTA already running);
  an explicit **Approve** applies now (the user asked for it). Mirror the espota guard — back the UI
  task off during the write and don't flush LVGL mid-flash (see `/ota` skill notes, main.cpp uiTask).
- **HTTPS (GitHub direct path):** GitHub asset URLs are `https://` and 302-redirect to
  `objects.githubusercontent.com`. `HTTPUpdate` follows redirects, but a `WiFiClientSecure` +
  `setInsecure()` (or a pinned CA) is needed. This is the *only* place the GitHub-direct option
  costs more device code than the portal path — one more reason the portal (plain LAN HTTP) is the
  recommended source. Keep the secure path behind an `https://` check so a portal user never pays
  for it.

### When it runs

- **On boot**, once WiFi + time are up (after `registrarBegin()`), one check.
- **Periodically** from `netTask`, rate-limited like `registrarTick()` (e.g. every ~6 h; cheap, and
  a `Approve` press can force an immediate re-check). Reuse the netTask cadence; do **not** add a
  task — the check is a single short HTTP GET plus a JSON parse.

### Config surface (`core/webconfig.*` + each unit's `:8080` page)

Extend the existing apply-layer rather than teaching boards about settings:

- **`webConfigApply` new fields:** `"otaAuto"` (`"0"`/`"1"`), `"updateUrl"` (validated `http(s)://…`),
  and an **action** `"updateNow"` (approve/trigger — validates there *is* an available target, then
  arms the updater). Same return-`false`-with-`err` convention as today.
- **`webConfigJson`** gains an `ota` block: `{auto, updateUrl, running: FW_VERSION, available:
  <version|null>}` so the config page renders the toggle, the URL field, current-vs-available, and
  an **Approve** button that appears only when `available && !auto`.
- Each unit's config page (nest/sleep/button all have one) grows an **Updates** section wired to the
  above. No per-unit logic — it's all in `webconfig`.

### Registration/heartbeat payload (`registrationJson` / `heartbeatJson`)

Add `otaAuto` and `updateAvailable` (the available version string or null) so the portal tile can
show status and decide whether to offer an Approve button — no new endpoint, just two more fields
on payloads the device already sends.

---

## Phase 3 — Portal as the LAN update source (optional, recommended)

**Goal:** make the portal a plain-HTTP LAN mirror of the latest Release so *no device touches
GitHub* and rollout is controllable from the one dashboard that already lists every device. Skip
this and everything still works via the GitHub-direct `updateUrl`; this is the "fully local + one
console" upgrade.

### Portal changes (`sonos-portal/`)

- **Release poller** (background thread, like `_probe_loop`): every ~15 min hit the GitHub Releases
  API for the repo's latest, and if its tag != the cached one, download the three `firmware-*.bin`
  + `manifest.json` into `DATA_DIR/firmware/`. One WAN dependency, on the *portal* (a server, by
  definition), never on a device. Configurable repo + optional token via env/add-on options.
- **New endpoints:**
  - `GET /api/firmware?id=&fw=&unit=` — serve the cached manifest, **rewriting each `url`** to the
    portal's own `http://<portal>/firmware/<bin>` (LAN, plain HTTP), and injecting per-device
    `"approved"` from the approvals map (below).
  - `GET /firmware/<file>.bin` — stream a cached binary (a `FileResponse`; supports Range).
  - `POST /api/devices/{id}/approve` — dashboard "Approve/Update" → set `approved[id]=<version>`;
    cleared once the device's heartbeat reports it's running that version. `POST …/approve-all` for
    the fleet.
- **Registry:** store `otaAuto` + `updateAvailable` from the payload; expose in `/api/devices`.

### Dashboard (`static/index.html`)

Per tile: `running vX` → `update vY available` when they differ; an **Update** button when the
device is `otaAuto=false` (calls `…/approve`); a top-bar **Update all** and a portal-wide "firmware
vY mirrored" line. Reuses the existing tile/probe rendering.

Point each device's `updateUrl` at `http://<portal>/api/firmware` (or let it default there when the
portal is known — OPEN QUESTION) and the entire path — build, store, distribute, approve — is
GitHub-for-CI + LAN-for-everything-else, zero cloud between the portal and any device.

---

## Testing

- **Phase 1:** `workflow_dispatch` a build; confirm three artifacts + a well-formed `manifest.json`;
  tag a throwaway `v0.0.0-test`, confirm the Release assets; `espota` a downloaded asset onto a real
  device (proves the CI `.bin` boots — the thing most likely to differ from a laptop build, e.g. a
  missing `secrets.h` macro).
- **Phase 2:** on a device, set `updateUrl` to a hand-hosted manifest pointing at an *older* tag's
  bin → expect "update available", no auto-apply with `otaAuto=false`; flip `otaAuto=true` (or press
  Approve) → expect `HTTPUpdate` runs, device reboots on the new version, tile updates. Verify a
  bad/partial download leaves the running firmware intact (pull the URL mid-transfer). Verify it
  never applies while streaming (`localAudioActive()` on sleep/button).
- **Phase 3:** portal poller mirrors a real Release; a device pointed at `/api/firmware` sees the
  rewritten LAN url and updates over plain HTTP; the dashboard Approve button drives an
  `otaAuto=false` device; `approve-all` rolls the fleet. mDNS-advertise is untestable in WSL (no
  multicast) — the API path is testable with direct HTTP as in plans/05.

## Open questions

- **Default `updateUrl` to the portal when known?** When `settingsPortal()` is set but `updateUrl`
  is empty, should the updater implicitly use `http://<portal>/api/firmware`, or stay strictly
  opt-in (empty = off, full stop)? Implicit-default is the least-config fleet experience; strict
  opt-in is the more defensible reading of the no-server promise. Leaning **strict opt-in** (a
  device never reaches for an update source it wasn't told to), with the portal's dashboard offering
  a one-click "point my devices here" that sets `updateUrl` explicitly.
- **Auto-apply timing default:** on-next-reboot vs. next-idle-window. Reboot is the least surprising
  (never interrupts a playing device); idle-window is faster to land. Leaning **reboot** for
  `otaAuto`, immediate only on explicit Approve.

## Non-goals / future

- **Signed / secure-boot OTA.** LAN + plain HTTP is the threat model. `sha256` is in the manifest to
  make image verification a later drop-in (`Update.setMD5`/hash check before commit).
- **Staged/canary rollout, rollback-on-failure telemetry.** The dual-OTA slot already gives implicit
  rollback (a bad image never commits); explicit "roll the fleet back to vX" can layer on the
  approvals map later.
- **Delta/compressed images.** Full-image only; slots are wide (3.3–6 MB) and LAN transfer is fast.

## Touch list (files)

- **New:** `.github/workflows/firmware.yml`, `tools/build_manifest.py`,
  `src/core/net/updater.{h,cpp}`.
- **Edit (firmware):** `settings.{h,cpp}` (+otaAuto/updateUrl), `webconfig.{h,cpp}` (+ota fields,
  +updateNow action, +ota block, +payload fields), each unit's `:8080` handler to render the
  Updates section, `app.cpp`/`netTask` to call `updaterBegin()`/`updaterTick()`, `main.cpp` UI
  back-off already covers OTA-active.
- **Edit (portal):** `app/main.py` (+endpoints, +poller), `app/registry.py` (+ota fields,
  +approvals), `app/static/index.html` (+Update buttons/diff), add-on options for repo/token.
```
