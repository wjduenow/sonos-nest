# Sonos Device Registration Portal

> Status: **Part A + Part B built** (2026-07-19). Portal (FastAPI, `sonos-portal/`) and firmware
> self-registration (`src/core/net/registrar.*`) implemented and verified; Part C (logs) deferred.
> Hardware pass (flash a device, watch it appear) pending.

## Context

Today there is no single place to see all the sonos-nest ESP32 devices on the LAN or reach
each one's web config. Devices are only *passively* discoverable (mDNS via ArduinoOTA on
`<name>.local`, DHCP hostname) and never announce themselves to any server. Two of the three
unit types (`sleep-machine` es3c28p, `sonos-button` esp32s3cam) serve a web config UI on
`:8080`; the round **nest** (`crowpanel_rotary`) has **no HTTP surface at all**
(`localManagerUrl()` returns `nullptr`, `board.cpp:33-34`). There is no firmware version string
and no networked log path anywhere (Serial/`readser.py` only).

The goal: a small **local** server that every device self-registers with, giving one dashboard
to single-click into each device's web config (and, later, view logs). It must run **either** as
a standalone Docker image **or** as a Home Assistant add-on, and devices should self-discover it.

### Decisions (confirmed with user)
- **Discovery:** device *push* + portal-side *pull* fallback. Portal advertises
  `_sonosportal._tcp` over mDNS; each device discovers it and POSTs a registration + heartbeat at
  boot. This works for **all** units — including the nest, which has no web server. Portal also
  browses mDNS as a safety net for devices that haven't registered.
- **Codebase:** **one** app / one Docker image, with a thin Home Assistant add-on wrapper
  (a HA add-on *is* a Docker container + manifest). ~95% shared; single repo.
- **Stack:** Python + FastAPI + `python-zeroconf`. Registry in a JSON file under `/data`. Static
  single-page frontend.
- **Logs:** deferred to Phase 2 (needs new firmware plumbing; none exists today).

---

## Part A — Portal server (`sonos-portal/`)

A single FastAPI app packaged two ways from one image.

### Layout (as built)
```
sonos-portal/
  app/
    main.py            # FastAPI: registration API + dashboard, ingress-aware (X-Ingress-Path)
    registry.py        # device store (JSON under DATA_DIR, default /data) + mDNS-seen fallback
    mdns.py            # zeroconf: advertise _sonosportal._tcp + browse _arduino._tcp fallback
    static/index.html  # dashboard SPA (device tiles -> open config URL in new tab)
  Dockerfile           # standalone image (python:3.12-slim)
  docker-compose.yml   # standalone run (network_mode: host for mDNS)
  requirements.txt
  sample.json          # a register payload, for the curl test
  homeassistant-addon/ # thin HA wrapper over the SAME image
    config.yaml        #   manifest: ingress, host_network, portal_host option
    build.yaml         #   BUILD_FROM per arch (the base image)
    Dockerfile         #   FROM the base image + run.sh
    run.sh             #   reads /data/options.json -> env, starts uvicorn
  README.md
```

### HTTP surface
- `POST /api/register` — device announces itself (identity from `registrationJson()`): `deviceName`,
  `mdnsName`, `ip`, `unit` (nest/sleep/button), `board`, `fwVersion`, `configUrl` (may be null for
  the nest), `zones[]`. Upsert keyed by `mdnsName`.
- `POST /api/heartbeat` — periodic liveness (`mdnsName`, `ip`, `uptimeSec`, `fwVersion`). Marks the
  device online; a missing heartbeat window flips it offline. `404` if unknown → firmware re-registers.
- `GET  /api/devices` — dashboard data (registered + mDNS-seen, with `online`/`status`).
- `GET  /api/portal` — the portal's own mDNS advertisement info (diagnostics).
- `GET  /` — dashboard SPA: one tile per device. **Open config** (`window.open(configUrl)`) when
  set; disabled with a note for the nest.

### Discovery (mdns.py, `python-zeroconf`)
- **Advertise** `_sonosportal._tcp.local` with the portal's host IP + port.
- **Browse fallback:** watch `_arduino._tcp` (advertised by every device via ArduinoOTA) to list
  devices that haven't registered — shown as "seen, awaiting registration".
- Requires container **host networking** (mDNS multicast doesn't cross Docker's default bridge).
  `network_mode: host` in compose + `host_network: true` in the add-on manifest.

### Standalone vs Home Assistant — the only deltas
The image is identical. HA glue, all thin:
- `config.yaml`: `ingress: true`, `host_network: true`, a `portal_host` option; `/data` is
  automatic for add-ons.
- **Ingress base path:** the SPA uses **relative** URLs and the app honors `X-Ingress-Path`
  (reflected into ASGI `root_path`), so it works at `/` (standalone) and under
  `/api/hassio_ingress/<token>/`.
- Options read from `/data/options.json` (HA, via `run.sh`) vs env vars / compose (standalone).

---

## Part B — Firmware: self-registration (shared core, all units)

Lives in `src/core/` so it ships to all three envs.

### `src/core/net/registrar.{h,cpp}`
- `registrarBegin()` — mDNS-query for `_sonosportal._tcp` (via `ESPmDNS`), cache resolved `ip:port`
  in NVS (`settingsPortal()`), POST `/api/register` with `registrationJson()`.
- `registrarTick()` — periodic `POST /api/heartbeat` (~45 s), self-rate-limited, called from
  `netTask`. Also the retry path: re-resolves + re-registers if the portal was never found or a
  heartbeat fails (portal restarted/moved).

### Payload builder — reuse
`registrationJson()` added to `src/core/webconfig.cpp` (sibling of `webConfigJson()`): `deviceName`,
`mdnsName`, `ip`, `unit`/`board` (compile-time from the env macro), `fwVersion` (`FW_VERSION`),
`configUrl` (`localManagerUrl()`, null on the nest), `zones[]`.

### Wire-up
- `app.cpp appBoot()` — `registrarBegin()` after `otaBegin()` **and** `selectZone()` (so the first
  payload has zones).
- `app.cpp netTask` — `registrarTick()` on the existing loop.
- `platformio.ini` — `extra_scripts = pre:tools/git_version.py` in `[env]` injects
  `-DFW_VERSION` (git describe) into every build.
- NVS: `settingsPortal()`/`settingsSetPortal()` cache the resolved `ip:port`.

### Explicitly NOT changing
- No new per-board web server (nest stays HTTP-less; registration is an *outbound* POST).
- No change to the existing `:8080`/`:8081` servers, SSDP, or OTA.

---

## Part C — Phase 2 (deferred): logs
Add a Serial ring buffer in core (tee `Serial` writes), include the last N lines in the heartbeat,
add a Logs view + `POST /api/log` to the portal. No firmware log surface exists today, so this is
net-new plumbing, sequenced after the core portal works.

---

## Verification

**Portal (done, standalone):** `uvicorn` up; `POST /api/register` (with and without `configUrl`)
→ device appears at `GET /api/devices` and on `/`; `POST /api/heartbeat` 200 known / 404 unknown;
registry persists to `devices.json`. mDNS advertise is untestable in the WSL sandbox (no multicast)
but degrades gracefully — the API still serves.

**Firmware (done, build):** `nest` and `sleep-button` envs build clean with the shared registrar.

**Pending — hardware pass:**
1. `docker compose up` on the LAN; `avahi-browse -rt _sonosportal._tcp` shows it.
2. Flash a unit (USB or `/ota`); watch boot serial for the portal mDNS resolve + `POST /api/register`;
   device appears on the dashboard within seconds.
3. Nest registers with `configUrl` null → shown present, config disabled.
4. Restart the portal → devices re-register (or re-found via the `_arduino._tcp` browse fallback).
5. Home Assistant: install the local add-on, enable Ingress, confirm the dashboard renders under the
   sidebar and devices still register (host networking on).
