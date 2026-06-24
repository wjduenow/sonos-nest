# Flashing & testing from WSL2 (Windows)

WSL2 doesn't see USB serial devices by default. The clean fix is
[`usbipd-win`](https://github.com/dorssel/usbipd-win), which bridges the device from
Windows into WSL. This guide goes from zero to serial output for the Phase-0 bring-up test.

> **Nothing to copy.** All firmware lives in this repo and `include/lv_conf.h` is committed.
> The Phase-0 bring-up test needs no WiFi/secrets. The flow is: install toolchain → bridge
> USB → build → flash → watch serial.

---

## Part 1 — Install PlatformIO Core in WSL (one-time)

In your **WSL terminal**:

```bash
sudo apt update && sudo apt install -y python3 python3-venv python3-pip usbutils
curl -fsSL -o /tmp/get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
python3 /tmp/get-platformio.py
```

Add `pio` to your PATH (and make it stick):

```bash
echo 'export PATH="$PATH:$HOME/.platformio/penv/bin"' >> ~/.bashrc
source ~/.bashrc
pio --version          # should now print a version
```

Add yourself to `dialout` so you don't need sudo for the serial port:

```bash
sudo usermod -aG dialout $USER
```

Log out/in of WSL (`exit`, then reopen) for the group to take effect — or just use `sudo`
on the upload/monitor commands.

---

## Part 2 — Install usbipd-win on Windows (one-time)

In an **Administrator PowerShell** (on Windows, not WSL):

```powershell
winget install --interactive --exact dorssel.usbipd-win
```

Close and reopen the admin PowerShell afterward so `usbipd` is on PATH. Requires
Windows 10 (21H2+) or 11 with WSL2.

---

## Part 3 — Attach the device to WSL

1. **Plug the CrowPanel into a USB-C port** (use a *data* cable, not charge-only).

2. In **admin PowerShell**, list USB devices:
   ```powershell
   usbipd list
   ```
   Find your board — it shows as `USB Serial Device`, `CP210x`, `CH340`, or
   `USB JTAG/serial debug unit`. Note its **BUSID** (e.g. `2-4`).

3. Bind it (one-time per device), then attach to WSL:
   ```powershell
   usbipd bind --busid 2-4
   usbipd attach --wsl --busid 2-4
   ```
   Replace `2-4` with your actual BUSID.

4. Back in **WSL**, confirm the device appeared:
   ```bash
   ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
   dmesg | tail -n 20
   ```
   You should see `/dev/ttyACM0` (ESP32-S3 native USB) or `/dev/ttyUSB0` (CH340/CP2102
   bridge). Remember which — call it `$PORT` below.

> ⚠️ **The attach does not survive an unplug or PC reboot.** After replugging, re-run
> `usbipd attach --wsl --busid 2-4`. To auto-reattach, run this and leave it open:
> `usbipd attach --wsl --busid 2-4 --auto-attach`.

---

## Part 4 — Build, flash, and monitor

In **WSL**, from the project root:

```bash
cd /home/wesd/Projects/sonos-nest

# First build pulls the ESP32 platform + toolchain + libs (~hundreds of MB, needs internet).
# This compiles the Phase-0 self-test (the `bringup` env).
pio run -e bringup
```

If it compiles clean, flash and open the serial monitor:

```bash
pio run -e bringup -t upload --upload-port /dev/ttyACM0
pio device monitor -e bringup -p /dev/ttyACM0 -b 115200
```

Swap `/dev/ttyACM0` for `/dev/ttyUSB0` if that's what showed up. Prefix with `sudo` if you
skipped the dialout step. Exit the monitor with `Ctrl+]`.

---

## Part 5 — What you should see

The serial monitor prints the self-test, and the screen shows a test pattern. Verify in order:

1. `I2C scan:` lists **`0x21` (PCF8574)** and **`0x15` (CST816 touch)**.
2. **Screen:** four quadrants — **TL=red, TR=green, BL=blue, BR=white** — then a backlight
   fade down/up.
   - *Wrong/garbled colors* → RGB pin map issue.
   - *Red and blue swapped* → uncomment the `lv_draw_sw_rgb565_swap(...)` line in
     `src/hw/display.cpp` (or flip the BGR flag).
3. **Twist the knob** → `[encoder] delta=… total=…` (confirm direction + that one click =
   one detent; tune `COUNTS_PER_DETENT` in `src/hw/encoder.cpp` if off).
4. **Press the knob** → `[button ] PRESS` once per push.
5. **Touch the screen** → `[touch  ] x=… y=…` in range 0–479.

Once that all checks out, flash the real app to confirm the flicker-free-redraw gate:

```bash
pio run -e crowpanel-rotary -t upload --upload-port /dev/ttyACM0
```

You should get an animated spinner + "sonos-nest" that twisting/pressing updates live.

---

## Common snags

- **Upload fails / "no serial data" / can't connect:** put the ESP32-S3 in download mode
  manually — hold **BOOT**, tap **RST**, release **BOOT**, then re-run the upload. After a
  successful flash it resets normally.
- **Port vanished after upload:** native-USB ESP32-S3 boards re-enumerate on reset; if the
  port number changes, re-check `ls /dev/ttyACM*` and re-attach with usbipd if needed.
- **`/dev/ttyUSB0` never appears** (CH340/CP2102 boards): the WSL2 kernel usually includes
  `ch341`/`cp210x`, but very old kernels don't. Run `wsl --update` from Windows PowerShell.
- **`usbipd attach` errors about WSL:** make sure a WSL distro is running (open a WSL
  terminal first), and that you're on WSL **2** (`wsl -l -v` shows VERSION 2).

---

## Alternative: flash from Windows directly

If you'd rather skip usbipd, install PlatformIO (or VS Code + the PlatformIO extension) **on
the Windows side** and flash to the `COMx` port directly — Windows esptool talks to COM
ports natively, no bridging. The catch: building from a `\\wsl$\...` path can be flaky, so
you'd typically keep the project on the Windows filesystem. The usbipd path above keeps
everything in WSL, which is cleaner when the repo lives there.
