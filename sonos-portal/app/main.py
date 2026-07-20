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

from .mdns import MDNSService
from .registry import Registry

PORT = int(os.environ.get("PORT", "8000"))
HOST_OVERRIDE = os.environ.get("PORTAL_HOST") or None
STATIC_DIR = Path(__file__).parent / "static"

PROBE_INTERVAL = 25   # seconds between config-page reachability sweeps
PROBE_TIMEOUT = 2     # per-device probe timeout

registry = Registry()
mdns = MDNSService(registry, port=PORT, host_override=HOST_OVERRIDE)
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
    yield
    _stop.set()
    mdns.stop()


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
    return {"ok": True}


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
