# sonos-portal

A small **local** dashboard that every [sonos-nest](../) device self-registers with. One page
listing all your ESP32 Sonos controllers on the LAN, each with a single click into its web config
— including the round **nest**, which has no web server of its own but still appears (config
disabled) because registration is an outbound POST.

One FastAPI app, packaged two ways from **one image**: standalone Docker, or a Home Assistant
add-on (a container + a thin manifest). ~95% shared code; the only HA-specific accommodation is
ingress path handling.

![Sonos Nest Portal dashboard](docs/dashboard.png)

Each tile shows the device's IP, firmware version, board, uptime and last heartbeat, an
**OTA-ready** chip, a live **config-page reachability** check, and a one-click link into its web
config. The Sonos zone list is shown once at the top (it's the same for every device).

## How discovery works

- The portal advertises `_sonosportal._tcp` over mDNS.
- Each device resolves that at boot (`src/core/net/registrar.cpp`) and POSTs `/api/register`
  (identity: name, IP, unit, board, firmware version, web-config URL, known Sonos zones), then
  heartbeats `/api/heartbeat` every ~45 s. A missed heartbeat window flips its tile offline.
- **Fallback:** the portal also browses `_arduino._tcp` (every unit runs ArduinoOTA), so a device
  that hasn't registered yet still shows up as "seen, awaiting registration".

Because mDNS multicast doesn't cross Docker's default bridge, the container **must use host
networking** (compose `network_mode: host` / add-on `host_network: true`).

### Device lifecycle

- A device that stops heartbeating flips to **offline** after ~2 min (greyed tile, OTA-ready
  chip drops, config probe shows ✗).
- **Removed a device for good?** Its offline tile shows a **Remove** button →
  `DELETE /api/devices/{id}` forgets it. It reappears automatically if it ever registers again,
  so removing is safe.
- **Auto-expiry:** a device offline for **7 days** is dropped from the registry automatically —
  long enough that a unit unplugged for a trip survives.

## HTTP surface

| Method | Path                 | Purpose                                              |
|--------|----------------------|------------------------------------------------------|
| POST   | `/api/register`      | device announces its identity (upsert by mDNS name)  |
| POST   | `/api/heartbeat`     | liveness; `404` if unknown → firmware re-registers   |
| GET    | `/api/devices`       | dashboard data                                       |
| DELETE | `/api/devices/{id}`  | forget a device (the tile's Remove button)           |
| GET    | `/api/portal`        | the portal's own mDNS info (diagnostics)             |
| GET    | `/`                  | dashboard SPA                                         |

## Run standalone

```bash
cd sonos-portal
docker compose up --build        # host networking; dashboard at http://<host>:8000
```

The device registry persists to `./data/devices.json`. Override the advertised IP (if
auto-detect picks the wrong NIC) with `PORTAL_HOST` in `docker-compose.yml`.

### Run without Docker (dev)

```bash
cd sonos-portal
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
DATA_DIR=./data PORT=8000 uvicorn app.main:app --host 0.0.0.0 --port 8000
```

## Home Assistant add-on

This folder **is** the add-on (`config.yaml` + `Dockerfile` + `build.yaml` + `run.sh`); the HA
Supervisor builds it on-device — no registry or prebuilt image needed. The quickest path is to add
this repo as a custom add-on repository and install **Sonos Nest Portal** from the store.

**→ Full step-by-step: [INSTALL-HOMEASSISTANT.md](INSTALL-HOMEASSISTANT.md)**

## Verify

```bash
# 1. Portal advertising over mDNS:
avahi-browse -rt _sonosportal._tcp        # (or: dns-sd -B _sonosportal._tcp)

# 2. Simulate a device registering, then confirm it appears:
curl -X POST http://localhost:8000/api/register -H 'Content-Type: application/json' -d @sample.json
curl -s http://localhost:8000/api/devices | python3 -m json.tool

# 3. Open http://localhost:8000 — the device tile's "Open config" opens its :8080 page.
# 4. Stop heartbeating → the tile flips offline after ~2 min (STALE_SECONDS in registry.py).
```

## Firmware side

Self-registration is in the shared core (`src/core/net/registrar.{h,cpp}`), so **all** units ship
it. Version is injected at build time via `tools/git_version.py` (`git describe`) → `FW_VERSION`.
No new per-device web server; the nest registers with a null `configUrl` and shows as
present-but-config-disabled.
