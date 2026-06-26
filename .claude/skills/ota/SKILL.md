---
name: ota
description: Flash sonos-nest firmware to the running device over WiFi (ArduinoOTA), no USB/usbipd cable. Use when the user wants to deploy/update/flash the ESP32 firmware wirelessly — e.g. "OTA", "flash over wifi", "push this build to the knob", "deploy the firmware".
---

# OTA flash — sonos-nest

Push firmware to the running device over WiFi instead of USB. The device advertises as
`sonos-nest` and listens for ArduinoOTA on UDP **3232**. A failed/partial OTA is harmless —
the running firmware is only overwritten after a transfer completes 100%.

## Prerequisites (one-time per machine)

The build host must reach the device **and accept the device's TCP connect-back** (OTA has
the device connect back to push data).

- **WSL2 (mirrored networking):** inbound to WSL is blocked by default. In an **admin
  PowerShell**, allow it once:
  ```powershell
  Set-NetFirewallHyperVVMSetting -Name '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -DefaultInboundAction Allow
  ```
- Otherwise run `espota.py` from **Windows** directly (the host is on the LAN), pointing at
  the WSL-built `.bin` via `\\wsl.localhost\...`.

## Procedure

1. **Build** the app firmware:
   ```bash
   export PATH="$PATH:$HOME/.platformio/penv/bin"
   pio run -e crowpanel-rotary
   ```
   A transient GCC "internal compiler error / Segmentation fault" in the Arduino_GFX or
   FrameworkArduino step is flaky — just re-run `pio run`. It is not a real error.

2. **Find the device IP.** Read it off the on-device **Settings** screen (long-press the
   knob → Settings, shown under "OTA: sonos-nest"). DHCP can change it. Confirm reachability:
   ```bash
   ping -c1 -W2 <device-ip>
   ```
   (If you can't see the screen, the boot serial prints `[ota] ready as sonos-nest @ <ip>`.)

3. **Upload over WiFi** — run espota directly on the already-built binary (avoids rebuilding
   the separate `ota` env, which is more prone to the transient ICE):
   ```bash
   ESPOTA=$(find ~/.platformio/packages/framework-arduinoespressif32 -name espota.py | head -1)
   python3 "$ESPOTA" -i <device-ip> -p 3232 -f .pio/build/crowpanel-rotary/firmware.bin -r
   ```
   - Add `-a <password>` if the device firmware sets `OTA_PASSWORD` (in `include/secrets.h`).
   - Success ends with `100% Done...`; the device reboots into the new firmware.
   - Equivalent (but slower/flakier build): `pio run -e ota -t upload --upload-port <device-ip>`.

## Notes & troubleshooting

- **Errors early (~3%) then aborts:** the UI task is starving the OTA write. The firmware
  backs the UI task off to 120ms while `otaActive()` (see `src/main.cpp uiTask`) to prevent
  this — keep that behavior. If you see this, confirm that's present.
- **Stalled OTA:** the device auto-reboots after ~20s of no progress into the still-valid
  firmware (stall safety in `src/ui/screens.cpp` OTA overlay). If the screen looks frozen
  mid-update, just tap **RST** — nothing was written.
- **`ping` fails / IP wrong:** the IP changed (DHCP). Re-read it from the Settings screen.
- **Connect-back never completes (hangs at 0%):** the inbound-to-host path is blocked — do
  the firewall step above, or run espota from Windows.
- **Fallback:** USB flashing via usbipd always works — see `docs/flashing-wsl.md`.
