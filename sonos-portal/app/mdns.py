"""mDNS: advertise the portal, and browse for devices as a discovery fallback.

Two jobs, both via python-zeroconf on a background thread:

* **Advertise** ``_sonosportal._tcp.local`` with the portal's LAN IP + port. This is what every
  firmware device queries for (net/registrar.cpp) to find where to POST its registration.
* **Browse** ``_arduino._tcp`` — the service ArduinoOTA advertises on every unit. A device that
  hasn't registered (portal started after it, or an older firmware) still shows up as
  "seen, awaiting registration" so nothing on the LAN is invisible.

Requires **host networking** on the container — mDNS multicast (224.0.0.251:5353) doesn't cross
Docker's default bridge. See docker-compose.yml / the add-on's ``host_network: true``.
"""

from __future__ import annotations

import socket
from typing import Any

from zeroconf import ServiceBrowser, ServiceInfo, ServiceListener, Zeroconf

from .registry import Registry

SERVICE_TYPE = "_sonosportal._tcp.local."
ARDUINO_TYPE = "_arduino._tcp.local."

# Every ESP32 running ArduinoOTA advertises _arduino._tcp, so the browse alone can't tell a
# sonos-nest-project device from any other board on the LAN (and hostnames are user-settable, so
# they're no signal either). Our firmware tags its record with this compiled-in TXT key/value
# (see src/core/net/ota.cpp otaBegin()); devices without it are ignored by the discovery fallback.
SONOS_TXT_KEY = b"app"
SONOS_TXT_VAL = b"sonos-nest"


def _lan_ip(override: str | None) -> str:
    """The address to advertise. An explicit PORTAL_HOST wins; otherwise ask the kernel which
    local interface it would use to reach the LAN (no packet is actually sent)."""
    if override:
        return override
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


class _ArduinoListener(ServiceListener):
    """Feeds mDNS-discovered ArduinoOTA devices into the registry's 'seen' set."""

    def __init__(self, zc: Zeroconf, registry: Registry) -> None:
        self._zc = zc
        self._registry = registry

    def add_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        self._resolve(zc, type_, name)

    def update_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        self._resolve(zc, type_, name)

    def remove_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        # Leave it in 'seen'; it'll age out via last_seen rather than vanish on one missed packet.
        pass

    def _resolve(self, zc: Zeroconf, type_: str, name: str) -> None:
        info = zc.get_service_info(type_, name, timeout=2000)
        if not info:
            return
        # Only our firmware carries the sonos-nest TXT marker. Anything else advertising
        # _arduino._tcp (another project's ESP32, a stray dev board) is not ours — skip it so it
        # never creates a "seen" row on the dashboard.
        if info.properties.get(SONOS_TXT_KEY) != SONOS_TXT_VAL:
            return
        addrs = info.parsed_addresses()
        ip = addrs[0] if addrs else ""
        # info.server is like "sonos-nest.local." — normalize to the "<name>.local" id the
        # firmware registers under, so a later real registration promotes the same row.
        dev_id = (info.server or name).rstrip(".")
        short = dev_id.split(".")[0]
        # This device is advertising ArduinoOTA right now → OTA-ready (registered or not).
        self._registry.note_ota(ip)
        self._registry.note_seen(dev_id, short, ip)


class MDNSService:
    def __init__(self, registry: Registry, port: int, host_override: str | None = None) -> None:
        self._registry = registry
        self._port = port
        self._host = _lan_ip(host_override)
        self._zc: Zeroconf | None = None
        self._info: ServiceInfo | None = None
        self._browser: ServiceBrowser | None = None

    def start(self) -> None:
        self._zc = Zeroconf()
        self._info = ServiceInfo(
            SERVICE_TYPE,
            f"sonos-portal.{SERVICE_TYPE}",
            addresses=[socket.inet_aton(self._host)],
            port=self._port,
            properties={"path": "/"},
            server="sonos-portal.local.",
        )
        self._zc.register_service(self._info)
        # Fallback discovery of un-registered devices.
        self._browser = ServiceBrowser(
            self._zc, ARDUINO_TYPE, _ArduinoListener(self._zc, self._registry)
        )
        print(f"[mdns] advertising {SERVICE_TYPE} at {self._host}:{self._port}", flush=True)

    def stop(self) -> None:
        if self._zc and self._info:
            try:
                self._zc.unregister_service(self._info)
            except Exception:
                pass
        if self._zc:
            self._zc.close()

    def info(self) -> dict[str, Any]:
        return {"host": self._host, "port": self._port, "service": SERVICE_TYPE}
