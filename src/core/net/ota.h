// OTA firmware updates over WiFi (ArduinoOTA / espota). Frees you from the USB/usbipd
// flashing flow. See plan §7 Phase 4 and docs/flashing-wsl.md.
//   pio run -e ota -t upload                 (resolves sonos-nest.local via mDNS)
//   pio run -e ota -t upload --upload-port <device-ip>
// The build host must be able to reach the device's IP on the LAN.
#pragma once

// The mDNS/OTA name this firmware advertises (the compile-time DEVICE_HOSTNAME). Single source
// of truth — this is NOT the DHCP hostname (see wifiHostname()), which the user can change at
// runtime. Anything that displays "where do I reach this device" must not conflate the two.
const char *otaHostname();

void otaBegin();    // start the OTA listener (call after WiFi connects)
void otaHandle();   // pump the OTA handler (call from loop())
bool otaActive();   // true while an update is in progress (pause heavy tasks)
int  otaProgress(); // 0..100 during an update, else -1
