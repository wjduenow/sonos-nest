// Copy to include/secrets.h (gitignored) and fill in for development.
// Production should store creds in NVS via the captive portal instead.
#pragma once

#define WIFI_SSID  "your-ssid"
#define WIFI_PASS  "your-password"

// Optional: bypass SSDP during dev by pointing straight at a speaker (plan §7).
// #define SONOS_ZONE_IP  "192.168.1.50"

// Optional: pin the room the knob controls, and the clock timezone (POSIX TZ).
// #define SONOS_DEFAULT_ROOM "Master Bedroom"
// #define CLOCK_TZ           "PST8PDT,M3.2.0,M11.1.0"

// Optional: require a password for OTA updates (recommended once OTA is reachable on your
// LAN). Flash this once over USB so the running firmware expects it; then OTA with
//   pio run -e ota -t upload --upload-port <ip> --upload-flags="--auth=<pass>"
// #define OTA_PASSWORD "choose-a-password"
