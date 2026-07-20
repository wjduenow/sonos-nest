"""sonos-portal — a small LAN dashboard every sonos-nest device self-registers with.

One FastAPI app, packaged two ways from one image: standalone (``docker run``) or a Home
Assistant add-on (a container + manifest). The only HA-specific accommodation is ingress: HA
serves the UI under ``/api/hassio_ingress/<token>/``, so the SPA uses **relative** URLs and this
app honors ``X-Ingress-Path`` (via ``root_path``) — the same code renders at ``/`` standalone.

HTTP surface:
    POST /api/register   device announces its identity (net/registrar.cpp → registrationJson())
    POST /api/heartbeat  periodic liveness; 404 if unknown → firmware re-registers
    GET  /api/devices    dashboard data
    GET  /api/portal     portal's own mDNS advertisement info (diagnostics)
    GET  /               dashboard SPA (single self-contained HTML file)
"""

from __future__ import annotations

import os
import threading
import urllib.error
import urllib.request
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.responses import FileResponse, JSONResponse

from .firmware import FirmwareMirror
from .mdns import MDNSService
from .registry import Registry

PORT = int(os.environ.get("PORT", "8000"))
HOST_OVERRIDE = os.environ.get("PORTAL_HOST") or None
STATIC_DIR = Path(__file__).parent / "static"

PROBE_INTERVAL = 25   # seconds between config-page reachability sweeps
PROBE_TIMEOUT = 2     # per-device probe timeout

# Firmware mirror (plans/06 Part 3) — off unless FIRMWARE_REPO (owner/name) is set.
FIRMWARE_REPO = os.environ.get("FIRMWARE_REPO") or None
FIRMWARE_TOKEN = os.environ.get("FIRMWARE_TOKEN") or None
FIRMWARE_POLL = int(os.environ.get("FIRMWARE_POLL", "900"))

registry = Registry()
mdns = MDNSService(registry, port=PORT, host_override=HOST_OVERRIDE)
mirror = FirmwareMirror(repo=FIRMWARE_REPO, token=FIRMWARE_TOKEN, interval=FIRMWARE_POLL)
_stop = threading.Event()


def _probe_config(url: str) -> bool:
    """Is the device's config page answering right now? Any HTTP response (even 4xx) counts as
    reachable; only a connection/timeout failure is 'unreachable'."""
    try:
        req = urllib.request.Request(url, method="GET")
        with urllib.request.urlopen(req, timeout=PROBE_TIMEOUT) as r:
            return r.status < 500
    except urllib.error.HTTPError as e:
        return e.code < 500
    except Exception:
        return False


def _probe_loop() -> None:
    while not _stop.is_set():
        for dev_id, cfg in registry.registered_targets():
            if cfg:
                registry.set_config_reachable(dev_id, _probe_config(cfg))
        _stop.wait(PROBE_INTERVAL)


def _start_mdns() -> None:
    try:
        mdns.start()
    except Exception as exc:  # mDNS failing (e.g. no host networking) must not kill the API
        print(f"[main] mDNS start failed: {exc} — registration still works via direct POST", flush=True)


@asynccontextmanager
async def lifespan(app: FastAPI):
    # Start mDNS off the event loop: binding zeroconf can block for several seconds (and hangs on
    # a bridge network without multicast), and HA's Supervisor has a startup watchdog. The API must
    # be serving immediately; discovery comes up whenever it can.
    threading.Thread(target=_start_mdns, name="mdns-start", daemon=True).start()
    threading.Thread(target=_probe_loop, name="config-probe", daemon=True).start()
    mirror.start()  # spawns its own daemon poll thread (no-op if FIRMWARE_REPO is unset)
    yield
    _stop.set()
    mdns.stop()
    mirror.stop()


app = FastAPI(title="Sonos Nest Portal", lifespan=lifespan)


@app.middleware("http")
async def ingress_root_path(request: Request, call_next):
    # Behind HA ingress, X-Ingress-Path is the prefix the UI is mounted under. Reflecting it into
    # the ASGI root_path keeps any absolute links correct; the SPA itself uses relative URLs, so
    # this is belt-and-suspenders for both modes.
    ingress = request.headers.get("X-Ingress-Path")
    if ingress:
        request.scope["root_path"] = ingress.rstrip("/")
    return await call_next(request)


@app.post("/api/register")
async def register(request: Request):
    payload = await request.json()
    dev_id = registry.register(payload)
    return {"ok": True, "id": dev_id}


@app.post("/api/heartbeat")
async def heartbeat(request: Request):
    payload = await request.json()
    if not registry.heartbeat(payload):
        # Unknown device (portal forgot it / cleared state) → tell it to re-register.
        return JSONResponse({"ok": False, "error": "unknown device"}, status_code=404)
    resp = {"ok": True}
    # If an update is approved for this device, nudge it to re-check the manifest now — it otherwise
    # only polls every ~6 h, so this is what makes a dashboard "Update" land within a heartbeat.
    # registry.heartbeat() already cleared the approval if the device now runs that version, so a
    # converged device won't be nudged.
    dev_id = payload.get("mdnsName") or payload.get("ip")
    if dev_id and registry.pending_approval(dev_id):
        resp["recheck"] = True
    return resp


@app.get("/api/devices")
async def devices():
    devs = registry.devices()
    # The Sonos zone list is the same on every device, so surface it ONCE (union across devices)
    # instead of repeating it under each tile — the frontend shows a per-device count for a
    # cross-check and this global list at the top.
    union: dict[str, str] = {}
    for d in devs:
        for z in d.get("zones", []):
            if z.get("name"):
                union[z["name"]] = z.get("ip", "")
    zones = [{"name": n, "ip": ip} for n, ip in sorted(union.items())]
    return {"devices": devs, "zones": zones}


@app.delete("/api/devices/{dev_id}")
async def remove_device(dev_id: str):
    # Manual "forget" — the tile's Remove button. The device reappears if it registers again
    # (or is re-discovered over mDNS), so this is safe to use on anything that's really gone.
    return {"ok": True, "removed": registry.remove(dev_id)}


# --- firmware pull-OTA (plans/06 Part 3) -------------------------------------
@app.get("/api/firmware")
async def firmware_manifest(request: Request):
    """Device-facing manifest (net/updater.cpp GETs this as its updateUrl). Same schema CI emits,
    but every `url` is rewritten to a portal-served LAN address so the device never touches GitHub,
    and each unit carries a per-device `approved` derived from the query `id` — that's how an
    otaAuto=false device flashes only after the dashboard approves it."""
    m = mirror.manifest()
    if not m:
        return JSONResponse({"error": "no firmware mirrored"}, status_code=404)
    version = m.get("version")
    dev_id = request.query_params.get("id")
    approved = registry.is_approved(dev_id, version) if dev_id else False
    base = str(request.base_url)  # e.g. http://<portal-ip>:8000/ — how the device reached us
    units = {}
    for unit, u in m.get("units", {}).items():
        units[unit] = {
            "bin": u.get("bin"),
            "url": f"{base}firmware/{u.get('bin')}",
            "sha256": u.get("sha256"),
            "size": u.get("size"),
            "approved": approved,
        }
    return {"version": version, "units": units}


@app.get("/firmware/{name}")
async def firmware_bin(name: str):
    """Stream a mirrored binary over plain LAN HTTP. bin_path() refuses any path traversal."""
    p = mirror.bin_path(name)
    if not p:
        return JSONResponse({"error": "not found"}, status_code=404)
    return FileResponse(p, media_type="application/octet-stream", filename=name)


@app.get("/api/firmware/status")
async def firmware_status():
    """Mirror state for the dashboard header (enabled, mirrored version, last check, last error)."""
    return mirror.status()


@app.post("/api/devices/{dev_id}/approve")
async def approve_device(dev_id: str):
    v = mirror.version()
    if not v:
        return JSONResponse({"ok": False, "error": "no firmware mirrored"}, status_code=409)
    registry.approve(dev_id, v)
    return {"ok": True, "approved": v}


@app.post("/api/devices/approve-all")
async def approve_all():
    """Approve every registered device that's currently reporting an available update."""
    v = mirror.version()
    if not v:
        return JSONResponse({"ok": False, "error": "no firmware mirrored"}, status_code=409)
    ids = [d["id"] for d in registry.devices() if d.get("updateAvailable")]
    registry.approve_all(v, ids)
    return {"ok": True, "approved": v, "count": len(ids)}


@app.get("/api/portal")
async def portal_info():
    return mdns.info()


@app.get("/health")
async def health():
    # Liveness probe for the container healthcheck — no auth, no I/O. autoheal (on the Pi)
    # restarts the container if this stops responding.
    return {"ok": True}


@app.get("/")
async def index():
    return FileResponse(STATIC_DIR / "index.html")
