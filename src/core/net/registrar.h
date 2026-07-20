// Self-registration with the sonos-portal dashboard (plans/05-registration-portal.md, Part B).
//
// Every unit — including the screen-less nest and button — announces itself to a small LAN
// server so there's one page listing all devices with a click-through to each one's web config.
// This is an *outbound* POST; it needs no HTTP server on the device, which is why the nest (with
// no web surface at all) can still appear on the dashboard.
//
// Discovery is mDNS: the portal advertises `_sonosportal._tcp`; we resolve it (and cache the
// ip:port in NVS so a later boot can register before a fresh query resolves). Device-agnostic —
// lives in core/, ships to all envs. Both entry points reuse the existing HTTPClient + ESPmDNS
// already pulled in by album art / ArduinoOTA, so they add no new dependency.
#pragma once

// Resolve the portal (mDNS, else the NVS-cached ip:port) and POST /api/register with this
// device's identity. Call once from appBoot() AFTER otaBegin() (ArduinoOTA starts mDNS) and
// after discovery, so the registration payload's zone list is populated. No-op if WiFi is down
// or the portal can't be found — registrarTick() keeps retrying.
void registrarBegin();

// Periodic liveness: POST /api/heartbeat (~45 s) so the dashboard can flip a device offline when
// it goes quiet. Also the retry path — if the portal was never resolved (it may have started
// after us) or a heartbeat fails (it may have restarted/moved), this re-resolves and
// re-registers. Call from netTask on its existing loop; it rate-limits itself.
void registrarTick();
