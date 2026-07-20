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
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.responses import FileResponse, JSONResponse

from .mdns import MDNSService
from .registry import Registry

PORT = int(os.environ.get("PORT", "8000"))
HOST_OVERRIDE = os.environ.get("PORTAL_HOST") or None
STATIC_DIR = Path(__file__).parent / "static"

registry = Registry()
mdns = MDNSService(registry, port=PORT, host_override=HOST_OVERRIDE)


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
    yield
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
    return {"devices": registry.devices()}


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
