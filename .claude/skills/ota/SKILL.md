---
name: ota
description: Flash sonos-nest firmware to the running device over WiFi (ArduinoOTA), no USB/usbipd cable. Use when the user wants to deploy/update/flash the ESP32 firmware wirelessly — e.g. "OTA", "flash over wifi", "push this build to the knob", "deploy the firmware".
---

# OTA flash — sonos-nest

Push firmware to the running device over WiFi instead of USB. The device advertises as
`sonos-nest` and listens for ArduinoOTA on UDP **3232**. A failed/partial OTA is harmless —
the running firmware is only overwritten after a transfer completes 100%.

## Step 0 — check the firewall (WSL2 only)

OTA has the device connect **back** to the build host to push data. On WSL2 with mirrored
networking, inbound to WSL is blocked by the Hyper-V firewall by default, which makes
uploads hang at 0%. Before uploading, test it (WSL can query Windows via `powershell.exe`):

```bash
powershell.exe -NoProfile -Command "(Get-NetFirewallHyperVVMSetting -Name '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}').DefaultInboundAction" 2>/dev/null | tr -d '\r'
```

- Prints **`Allow`** → inbound is open, OTA can complete. Proceed.
- Prints **`Block`** (or nothing / errors) → inbound is blocked. **Tell the user to open it**:
  have them run this **once** in an **Administrator PowerShell** on Windows, then re-test:
  ```powershell
  Set-NetFirewallHyperVVMSetting -Name '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -DefaultInboundAction Allow
  ```
  (`Set-` needs admin and is why the user must run it; the read above usually works without
  admin. To revert later: same command with `-DefaultInboundAction Block`.)

If you can't/won't change the firewall, the alternative is to run `espota.py` from **Windows**
directly (the host is on the LAN, so no inbound-to-WSL hop), pointing at the WSL-built `.bin`
via `\\wsl.localhost\...`. A one-time Windows Firewall "allow" prompt may appear — accept it.

Skip this step entirely on a native Linux/macOS build host.

## Procedure

1. **Build** the app firmware:
   ```bash
   export PATH="$PATH:$HOME/.platformio/penv/bin"
   pio run -e nest        # or: pio run -e sleep-machine (advertises sonos-sleep)
   ```
   A transient GCC "internal compiler error / Segmentation fault" in the Arduino_GFX or
   FrameworkArduino step is flaky — just re-run `pio run`. It is not a real error.

2. **Find the device IP via mDNS.** The device advertises ArduinoOTA over mDNS as
   `sonos-nest._arduino._tcp`. WSL2 has no built-in mDNS resolver (`ping sonos-nest.local`
   fails), so query the LAN directly with the bundled discovery script:
   ```bash
   python3 -c "import zeroconf" 2>/dev/null || pip install zeroconf --quiet
   python3 .claude/skills/ota/find_device.py        # prints "sonos-nest <ip>"
   ```
   It browses for ~5s and prints `<name> <ip>` for each match (exit 1 if none found). Then
   confirm reachability:
   ```bash
   ping -c1 -W2 <device-ip>
   ```
   Fallbacks if mDNS finds nothing (device just booted / multicast blocked): read the IP off
   the on-device **Settings** screen (Menu → Settings, shown under "OTA: sonos-nest"), or
   catch the boot serial line `[ota] ready as sonos-nest @ <ip>`. DHCP can change the IP.

3. **Upload over WiFi** — run espota directly on the already-built binary (avoids rebuilding
   the separate `ota` env, which is more prone to the transient ICE). This auto-passes the
   OTA password if one is set in `include/secrets.h`:
   ```bash
   ESPOTA=$(find ~/.platformio/packages/framework-arduinoespressif32 -name espota.py | head -1)
   PASS=$(grep -oP '#define\s+OTA_PASSWORD\s+"\K[^"]+' include/secrets.h 2>/dev/null)
   python3 "$ESPOTA" -i <device-ip> -p 3232 -f .pio/build/nest/firmware.bin -r ${PASS:+-a "$PASS"}
   ```
   - The device requires `OTA_PASSWORD` (set in `secrets.h`); a wrong/missing password fails
     auth. The `${PASS:+-a ...}` above handles it automatically.
   - Success ends with `100% Done...` then a `TimeoutError`/`NameError` from espota — that is
     **benign**: the device reboots on completion before espota's final ack. The upload
     succeeded if you saw `100% Done...`.
   - Equivalent (but slower/flakier build): `pio run -e nest-ota -t upload --upload-port <device-ip> --upload-flags="--auth=<password>"`.

## Notes & troubleshooting

- **Errors early (~3%) then aborts:** the UI task is starving the OTA write. The firmware
  backs the UI task off to 120ms while `otaActive()` (see `src/main.cpp uiTask`) to prevent
  this — keep that behavior. If you see this, confirm that's present.
- **Stalled OTA:** the device auto-reboots after ~20s of no progress into the still-valid
  firmware (stall safety in `src/ui/screens.cpp` OTA overlay). If the screen looks frozen
  mid-update, just tap **RST** — nothing was written.
- **`ping` fails / IP wrong:** the IP changed (DHCP). Re-run the mDNS discovery script
  (Step 2) to pick up the new address.
- **Connect-back never completes (hangs at 0%):** the inbound-to-host path is blocked — do
  the firewall step above, or run espota from Windows.
- **Fallback:** USB flashing via usbipd always works — see `docs/flashing-wsl.md`.
