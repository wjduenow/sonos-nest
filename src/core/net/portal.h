// SoftAP captive-portal WiFi provisioning — device-agnostic (core).
//
// Raises an open access point, serves a scan + join page on :80 with a wildcard DNS so the
// phone's "sign in to network" sheet pops automatically, and applies the chosen credentials
// through the proven wifiApply() (which persists on success and reverts on failure, so a typo
// can't strand the device). BLOCKS until the device successfully joins a network; on return the
// AP + DNS are torn down and WiFi is back in STA mode, connected.
//
// The CALLER decides when to enter it (see appBoot): genuinely no stored credentials, or the
// button held through power-on as a deliberate re-provision trigger. A merely-failed connect
// must NOT enter here — that would drop a configured device into AP mode on any brief router
// outage. See plans/04-sonos-button-plan.md, "Phase 5 in detail".
#pragma once

#include <Arduino.h>

// Raise the portal under AP name `apSsid` (open network) and block until joined.
void portalRun(const char *apSsid);
