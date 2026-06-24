// Copy to include/secrets.h (gitignored) and fill in for development.
// Production should store creds in NVS via the captive portal instead.
#pragma once

#define WIFI_SSID  "your-ssid"
#define WIFI_PASS  "your-password"

// Optional: bypass SSDP during dev by pointing straight at a speaker (plan §7).
// #define SONOS_ZONE_IP  "192.168.1.50"
