// OTA firmware updates over WiFi (ArduinoOTA / espota). Frees you from the USB/usbipd
// flashing flow. See plan §7 Phase 4 and docs/flashing-wsl.md.
//   pio run -e ota -t upload                 (resolves sonos-nest.local via mDNS)
//   pio run -e ota -t upload --upload-port <device-ip>
// The build host must be able to reach the device's IP on the LAN.
#pragma once

// The mDNS/OTA name this firmware advertises. Follows the device name (wifiHostname(): the NVS
// device name, else the DEVICE_HOSTNAME default) — the SAME source as the DHCP hostname — so two
// of the same unit don't collide on <default>.local. Latched at otaBegin(); a device-name change
// reboots to re-read it. Returns a pointer into a persistent static buffer (copy it, don't hold).
const char *otaHostname();

void otaBegin();    // start the OTA listener (call after WiFi connects)
void otaHandle();   // pump the OTA handler (call from loop())
bool otaActive();   // true while an update is in progress (pause heavy tasks)
int  otaProgress(); // 0..100 during an update, else -1
