// Firmware self-update over HTTP — the device *pull* path (plans/06-scalable-ota.md § Phase 2).
//
// Device-agnostic → lives in core/, ships to every unit. Complements the espota *push* path
// (net/ota.*): instead of a build host pushing to each device, a device fetches a manifest from a
// URL it was told about, and if a newer build is published *for its unit*, flashes itself via the
// Arduino core's HTTPUpdate (dual-OTA slots, already configured by the partition table).
//
// Opt-in and source-agnostic — the whole feature is dormant until settingsUpdateUrl() is set, so a
// shipped unit is pure espota (no server, no cloud). The manifest URL may point at the LAN portal
// (plain HTTP, no cloud) or a GitHub-hosted manifest (HTTPS). See settings.h otaAuto/updateUrl.
//
// Apply policy (per-device, from settings + the manifest):
//   - otaAuto=true  → applies at BOOT (updaterBegin), before playback starts. It deliberately does
//                     NOT self-apply mid-run — a running device only flashes on an explicit approve
//                     — so an auto device picks up an update on its next reboot. Conservative on
//                     purpose (never yanks firmware out from under a playing sleep-machine).
//   - explicit approve (updaterApprove(), from the config page's "Update now" or the portal's
//                     Approve button via a per-device "approved":true in the manifest) → applies now.
// "Update available" is reported regardless of policy (updaterAvailable) so a UI/dashboard can show
// it even when auto is off.
#pragma once

#include <Arduino.h>

void   updaterBegin();             // one check at boot (call after registrarBegin, WiFi up); may
                                   // auto-apply + reboot if otaAuto and an update is published.
void   updaterTick();              // periodic check from netTask; self-rate-limited (~6 h). Applies
                                   // only on an explicit approve / portal-approved manifest.
bool   updaterActive();            // true while a pull-flash is in progress — the UI/art tasks must
                                   // back off exactly as they do for otaActive(): flash writes
                                   // disable the cache, and other-core code running from flash
                                   // during a write FAULTS (the device resets mid-download).
bool   updaterAvailable();         // a newer firmware is published for this unit (for UI/payloads).
String updaterAvailableVersion();  // that version string, or "" when up-to-date / disabled.
void   updaterApprove();           // arm an immediate apply (config "updateNow" / dashboard Approve).
void   updaterForceCheck();        // re-check on the next tick, bypassing the rate limit (e.g. the
                                   // updateUrl just changed and the UI wants a fresh availability).
