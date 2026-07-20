"""Firmware mirror — the LAN update source for device pull-OTA (plans/06-scalable-ota.md Part 3).

CI (``.github/workflows/firmware.yml``) publishes a GitHub Release per ``v*`` tag: one
``firmware-<unit>.bin`` per unit plus a ``manifest.json`` (schema in plans/06). This mirror polls
that repo's *latest* release and, when the tag changes, downloads the manifest + every listed
binary into ``DATA_DIR/firmware/`` (verifying each sha256). ``main.py`` then serves those over
plain LAN HTTP so **no device ever talks to GitHub** — the whole distribution path is local.

Disabled unless ``FIRMWARE_REPO`` (``owner/name``) is set, so a portal with no repo configured is
exactly what it was before. A token (``FIRMWARE_TOKEN``) is optional — it raises the GitHub API
rate limit; asset downloads use the public ``browser_download_url`` (fine for a public repo).
"""

from __future__ import annotations

import hashlib
import json
import threading
import time
import urllib.request
from pathlib import Path
from typing import Any

from .registry import DATA_DIR


def _now() -> float:
    return time.time()


class FirmwareMirror:
    def __init__(
        self,
        repo: str | None,
        token: str | None = None,
        interval: int = 900,
        data_dir: Path | None = None,
    ) -> None:
        self._repo = (repo or "").strip() or None
        self._token = (token or "").strip() or None
        self._interval = max(60, interval)
        self._dir = (data_dir or DATA_DIR) / "firmware"
        self._lock = threading.Lock()
        self._manifest: dict[str, Any] | None = None   # last mirrored manifest (device-agnostic)
        self._version: str | None = None
        self._last_check: float = 0.0
        self._last_error: str | None = None
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._load()   # survive a portal restart without re-downloading

    def enabled(self) -> bool:
        return self._repo is not None

    # --- lifecycle -----------------------------------------------------------
    def start(self) -> None:
        if not self.enabled():
            print("[firmware] FIRMWARE_REPO not set — mirror disabled (pull-OTA source off)", flush=True)
            return
        self._thread = threading.Thread(target=self._loop, name="fw-mirror", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()

    def _loop(self) -> None:
        while not self._stop.is_set():
            try:
                self._check()
            except Exception as exc:  # a flaky API / rate limit must not kill the thread
                with self._lock:
                    self._last_error = str(exc)
                print(f"[firmware] check failed: {exc}", flush=True)
            self._stop.wait(self._interval)

    # --- the mirror ----------------------------------------------------------
    def _check(self) -> None:
        release = self._api(f"https://api.github.com/repos/{self._repo}/releases/latest")
        tag = release.get("tag_name")
        if not tag:
            return
        with self._lock:
            already = (tag == self._version and self._manifest is not None)
        if already:
            with self._lock:
                self._last_check = _now()
                self._last_error = None
            return

        assets = {a["name"]: a["browser_download_url"] for a in release.get("assets", [])}
        if "manifest.json" not in assets:
            raise RuntimeError(f"release {tag} has no manifest.json asset")
        manifest = json.loads(self._download(assets["manifest.json"]))

        self._dir.mkdir(parents=True, exist_ok=True)
        for u in manifest.get("units", {}).values():
            binname = u.get("bin")
            if binname not in assets:
                raise RuntimeError(f"manifest lists {binname} but release {tag} has no such asset")
            blob = self._download(assets[binname])
            want = u.get("sha256")
            if want and hashlib.sha256(blob).hexdigest() != want:
                raise RuntimeError(f"{binname} sha256 mismatch — skipping mirror of {tag}")
            tmp = self._dir / f"{binname}.tmp"
            tmp.write_bytes(blob)
            tmp.replace(self._dir / binname)  # atomic swap so a served bin is never half-written

        (self._dir / "manifest.json").write_text(json.dumps(manifest, indent=2))
        with self._lock:
            self._manifest = manifest
            self._version = manifest.get("version", tag)
            self._last_error = None
            self._last_check = _now()
        print(f"[firmware] mirrored {self._version} ({len(manifest.get('units', {}))} units)", flush=True)

    def _api(self, url: str) -> dict[str, Any]:
        with urllib.request.urlopen(urllib.request.Request(url, headers=self._headers()), timeout=15) as r:
            return json.loads(r.read())

    def _download(self, url: str) -> bytes:
        with urllib.request.urlopen(urllib.request.Request(url, headers=self._headers()), timeout=120) as r:
            return r.read()

    def _headers(self) -> dict[str, str]:
        h = {"Accept": "application/vnd.github+json", "User-Agent": "sonos-portal"}
        if self._token:
            h["Authorization"] = f"Bearer {self._token}"
        return h

    def _load(self) -> None:
        try:
            m = json.loads((self._dir / "manifest.json").read_text())
            self._manifest = m
            self._version = m.get("version")
        except (FileNotFoundError, ValueError, OSError):
            pass

    # --- reads (used by main.py's endpoints) ---------------------------------
    def manifest(self) -> dict[str, Any] | None:
        with self._lock:
            return json.loads(json.dumps(self._manifest)) if self._manifest else None

    def version(self) -> str | None:
        with self._lock:
            return self._version

    def bin_path(self, name: str) -> Path | None:
        """Resolve a served binary name to a file, refusing any path-traversal attempt."""
        if not name or "/" in name or "\\" in name or ".." in name:
            return None
        p = self._dir / name
        return p if p.is_file() else None

    def status(self) -> dict[str, Any]:
        with self._lock:
            units = sorted((self._manifest or {}).get("units", {}).keys())
            return {
                "enabled": self.enabled(),
                "repo": self._repo,
                "version": self._version,
                "units": units,
                "lastCheckSec": int(_now() - self._last_check) if self._last_check else None,
                "lastError": self._last_error,
            }
