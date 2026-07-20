"""Device registry — the portal's single source of truth for what's on the LAN.

Two kinds of entry, merged in :meth:`devices`:

* **registered** — a device POSTed ``/api/register``; we have its full identity (config URL,
  unit, firmware) and a rolling ``last_seen`` from heartbeats. Online when a heartbeat arrived
  within :data:`STALE_SECONDS`.
* **seen** — the mDNS browser saw a ``_arduino._tcp`` advertisement (every unit runs ArduinoOTA)
  but the device never registered. We know only its name + IP; shown as "awaiting registration".

State is a plain JSON file under ``DATA_DIR`` (``/data`` in a container, mapped to a volume /
the HA add-on's persistent store) so the list survives a portal restart without the devices
having to re-register. All access is guarded by a lock — the mDNS browser thread and the FastAPI
request handlers both touch it.
"""

from __future__ import annotations

import json
import os
import threading
import time
from pathlib import Path
from typing import Any

# A device is "online" if a heartbeat/registration landed within this window. Firmware beats
# every ~45 s, so ~2.5 misses marks it offline — long enough to ride out one dropped beat.
STALE_SECONDS = 120

# Auto-expiry: a device that hasn't been heard from in this long is dropped from the registry
# entirely (a permanently-removed unit shouldn't linger forever). Conservative on purpose — a
# unit unplugged for a long trip survives; you can still remove one immediately by hand.
EXPIRE_SECONDS = 7 * 24 * 3600

# A device is "OTA-ready" if it's currently advertising _arduino._tcp (ArduinoOTA) over mDNS.
# mDNS records refresh on their own TTL, so keep a generous window to ride over refresh gaps.
OTA_ADVERTISE_WINDOW = 300

# A config-reachability probe result is trusted for this long before it's considered stale
# (unknown) rather than shown as a possibly-outdated tick/cross.
CONFIG_PROBE_FRESH = 90

DATA_DIR = Path(os.environ.get("DATA_DIR", "/data"))


def _now() -> float:
    return time.time()


class Registry:
    def __init__(self, data_dir: Path | None = None) -> None:
        self._dir = data_dir or DATA_DIR
        self._path = self._dir / "devices.json"
        self._lock = threading.Lock()
        # id -> record. id is the device's stable mdnsName (e.g. "sonos-nest.local").
        self._registered: dict[str, dict[str, Any]] = {}
        # id -> {"name","ip","last_seen"} for mDNS-seen-but-unregistered devices.
        self._seen: dict[str, dict[str, Any]] = {}
        # ip -> last time it advertised _arduino._tcp (ArduinoOTA). Drives the OTA-ready flag.
        self._ota_ips: dict[str, float] = {}
        # id -> {"reachable": bool, "at": float}. Background config-page probe results (not
        # persisted — transient, re-probed on startup).
        self._probe: dict[str, dict[str, Any]] = {}
        self._load()

    # --- persistence ---------------------------------------------------------
    def _load(self) -> None:
        try:
            raw = json.loads(self._path.read_text())
            self._registered = raw.get("registered", {})
        except (FileNotFoundError, ValueError, OSError):
            self._registered = {}

    def _save_locked(self) -> None:
        # Only registered devices persist; mDNS-seen entries are re-discovered on each run.
        try:
            self._dir.mkdir(parents=True, exist_ok=True)
            tmp = self._path.with_suffix(".tmp")
            tmp.write_text(json.dumps({"registered": self._registered}, indent=2))
            tmp.replace(self._path)  # atomic — never leave a half-written file
        except OSError as exc:
            print(f"[registry] save failed: {exc}", flush=True)

    # --- mutations -----------------------------------------------------------
    def register(self, payload: dict[str, Any]) -> str:
        """Upsert a full registration. Returns the id used as the key."""
        dev_id = _device_id(payload)
        with self._lock:
            rec = self._registered.get(dev_id, {})
            rec.update(
                {
                    "id": dev_id,
                    "deviceName": payload.get("deviceName") or dev_id,
                    "mdnsName": payload.get("mdnsName") or dev_id,
                    "ip": payload.get("ip", rec.get("ip", "")),
                    "unit": payload.get("unit", rec.get("unit", "unknown")),
                    "board": payload.get("board", rec.get("board", "unknown")),
                    "fwVersion": payload.get("fwVersion", rec.get("fwVersion", "")),
                    "configUrl": payload.get("configUrl"),  # may be null (nest has no web UI)
                    "zones": payload.get("zones", rec.get("zones", [])),
                    "last_seen": _now(),
                    "registered_at": rec.get("registered_at", _now()),
                }
            )
            self._registered[dev_id] = rec
            self._seen.pop(dev_id, None)  # promotion: a registered device isn't merely "seen"
            self._save_locked()
        return dev_id

    def heartbeat(self, payload: dict[str, Any]) -> bool:
        """Refresh liveness. Returns False if the device isn't registered (caller → 404, which
        makes the firmware re-register from scratch)."""
        dev_id = _device_id(payload)
        with self._lock:
            rec = self._registered.get(dev_id)
            if rec is None:
                return False
            rec["last_seen"] = _now()
            if payload.get("ip"):
                rec["ip"] = payload["ip"]
            if payload.get("fwVersion"):
                rec["fwVersion"] = payload["fwVersion"]
            if payload.get("uptimeSec") is not None:
                rec["uptimeSec"] = payload["uptimeSec"]
            self._save_locked()
        return True

    def note_seen(self, dev_id: str, name: str, ip: str) -> None:
        """Record an mDNS-discovered device (fallback path). No-op if already registered."""
        with self._lock:
            if dev_id in self._registered:
                return
            self._seen[dev_id] = {"name": name, "ip": ip, "last_seen": _now()}

    def remove(self, dev_id: str) -> bool:
        """Forget a device now (the manual 'Remove' button). It reappears if it registers again."""
        with self._lock:
            existed = dev_id in self._registered or dev_id in self._seen
            self._registered.pop(dev_id, None)
            self._seen.pop(dev_id, None)
            self._probe.pop(dev_id, None)
            if existed:
                self._save_locked()
            return existed

    def _prune_locked(self, now: float) -> None:
        """Drop anything not heard from in EXPIRE_SECONDS. Caller holds the lock."""
        dead = [i for i, r in self._registered.items() if (now - r.get("last_seen", 0)) > EXPIRE_SECONDS]
        for i in dead:
            self._registered.pop(i, None)
            self._probe.pop(i, None)
        for i in [i for i, s in self._seen.items() if (now - s.get("last_seen", 0)) > EXPIRE_SECONDS]:
            self._seen.pop(i, None)
        if dead:
            self._save_locked()

    def note_ota(self, ip: str) -> None:
        """Record that this IP is advertising ArduinoOTA (_arduino._tcp) right now."""
        if not ip:
            return
        with self._lock:
            self._ota_ips[ip] = _now()

    def set_config_reachable(self, dev_id: str, reachable: bool) -> None:
        """Store the result of a background probe of a device's config page."""
        with self._lock:
            self._probe[dev_id] = {"reachable": reachable, "at": _now()}

    def registered_targets(self) -> list[tuple[str, str | None]]:
        """(id, configUrl) for every registered device — snapshot for the probe loop."""
        with self._lock:
            return [(rec["id"], rec.get("configUrl")) for rec in self._registered.values()]

    def _ota_ready(self, ip: str, now: float, online: bool, registered: bool) -> bool:
        # A registered device that's online is heartbeating our firmware, which starts ArduinoOTA
        # at boot — so it's flashable. That's the reliable signal (mDNS _arduino._tcp events don't
        # re-fire dependably after a portal restart). Independent mDNS observation also counts,
        # which is what makes 'seen' (unregistered) devices OTA-ready.
        if registered and online:
            return True
        t = self._ota_ips.get(ip)
        return t is not None and (now - t) <= OTA_ADVERTISE_WINDOW

    def _config_reachable(self, dev_id: str, now: float) -> bool | None:
        p = self._probe.get(dev_id)
        if not p or (now - p["at"]) > CONFIG_PROBE_FRESH:
            return None  # not probed yet / stale → unknown
        return bool(p["reachable"])

    # --- reads ---------------------------------------------------------------
    def devices(self) -> list[dict[str, Any]]:
        now = _now()
        out: list[dict[str, Any]] = []
        with self._lock:
            self._prune_locked(now)   # drop devices offline past EXPIRE_SECONDS
            for rec in self._registered.values():
                d = dict(rec)
                ip = rec.get("ip", "")
                d["online"] = (now - rec.get("last_seen", 0)) <= STALE_SECONDS
                d["status"] = "registered"
                d["lastSeenSec"] = int(now - rec.get("last_seen", 0))
                d["zoneCount"] = len(rec.get("zones", []))
                d["otaReady"] = self._ota_ready(ip, now, d["online"], True)
                d["configReachable"] = self._config_reachable(rec["id"], now)
                out.append(d)
            for dev_id, s in self._seen.items():
                if dev_id in self._registered:
                    continue
                out.append(
                    {
                        "id": dev_id,
                        "deviceName": s["name"],
                        "mdnsName": dev_id,
                        "ip": s["ip"],
                        "unit": "unknown",
                        "board": "unknown",
                        "fwVersion": "",
                        "configUrl": None,
                        "zones": [],
                        "zoneCount": 0,
                        "online": (now - s.get("last_seen", 0)) <= STALE_SECONDS,
                        "lastSeenSec": int(now - s.get("last_seen", 0)),
                        # seen == arduino-advertised, so its IP is already in _ota_ips
                        "otaReady": self._ota_ready(s["ip"], now, True, False),
                        "configReachable": None,
                        "status": "seen",  # discovered via mDNS, awaiting registration
                    }
                )
        out.sort(key=lambda d: (d["status"] != "registered", d.get("deviceName", "").lower()))
        return out


def _device_id(payload: dict[str, Any]) -> str:
    """Stable key: the mDNS name (unique per device), falling back to IP then a literal."""
    return payload.get("mdnsName") or payload.get("ip") or "unknown-device"
