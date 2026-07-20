# sonos-sleep-machine — the nightstand sleep-sound player

A touch-screen nightstand appliance that plays sleep sounds — either **on its own speaker**, or
**through a Sonos speaker** — and wakes you with a different track. Sound files live on a
microSD card, and you manage them from a **web page served by the device itself**, so the card
never has to come out.

Like the nest, it talks directly to Sonos over the local network. No server, no cloud, no phone
app.

Build env: **`sleep-machine`**. OTA/mDNS hostname: **`sonos-sleep`**.

---

## Hardware

**Board: Hosyond / LCDWIKI ES3C28P 2.8" ESP32-S3 display board** (the microSD-equipped
variant) — [Amazon B0FKG7WRWV](https://www.amazon.com/dp/B0FKG7WRWV).

| | |
|---|---|
| MCU | ESP32-S3R8 — dual-core 240 MHz, **8 MB OPI PSRAM, 16 MB flash**, WiFi 2.4 GHz |
| Display | **ILI9341V**, 240×320 over 4-wire SPI, run **rotated to landscape (320×240)**. Needs colour inversion on. |
| Touch | **FT6336G** capacitive, I²C `0x38` |
| Audio out | **ES8311 codec** + speaker (I²S), with an active-low amp enable |
| Storage | **microSD**, SDIO 4-bit, push-push socket on the back |
| Also on board | MEMS mic, addressable **WS2812 RGB LED** (IO42), battery ADC |
| I²C bus | SDA 16 / SCL 15 @ **100 kHz** (shared: touch + codec) |
| Power / programming | **USB-C**, with RESET + BOOT, all on one short edge |

Two board quirks worth knowing:

**The I²C bus runs at 100 kHz, not 400.** The FT6336 will ACK its address at 400 kHz but return
all-zero register reads — the bus routing can't sustain fast-mode. 100 kHz is rock-solid for
both the touch chip and the codec.

**The mic and the RGB LED are not wired up in firmware.** The hardware is there; nothing drives
them yet.

### Physical

Board is **86 × 50 × 1.6 mm**, corner radius R3.5, with **4 × Ø3.2 mounting holes** on a
42 × 78 mm rectangle. Glass stands ~4.3 mm proud of the PCB. Official drawing:
[`hardware/rec-2.8/ES3C28P_Size.pdf`](../hardware/rec-2.8/ES3C28P_Size.pdf).

### Case: angled nightstand stand

[`hardware/rec-2.8/countertop/`](../hardware/rec-2.8/countertop/) — a 3D-printed wedge that
holds the board **landscape, reclined 20° from vertical**, so it reads from a pillow.

| Part | File | What it is |
|---|---|---|
| Shell | `shell.stl` | The wedge body. Board drops into a reclined pocket and screws to 4 bosses. |
| Bezel | `bezel.stl` | Screwed-on front frame; the glass sits **flush** with its top face. |
| Speaker cap | `speaker_cap.stl` | Snaps in to close the rear speaker load port and back the speaker in. |

The stand provides a rear panel-mount USB-C jack, a RESET pin hole, microSD access, a mic port,
and a downward-firing speaker pocket. Geometry is generated with Python CSG (`trimesh` +
`manifold3d`): `python3 build_all.py`.

> **Before the final print:** the outline, mounting holes, glass and thickness are exact (from
> the manufacturer's drawing), but the **in-plane positions of USB-C / RESET / microSD / mic
> were estimated from board photos**. Caliper-verify them first. See
> [`hardware/rec-2.8/README.md`](../hardware/rec-2.8/README.md).

---

## What it does

### Three ways to play

The home screen is a **carousel** — swipe left/right to choose where the sound comes out, then
tap the big button:

| Mode | What happens |
|---|---|
| **Sonos** | Plays a sleep playlist from the Sonos speaker's own library. |
| **Stream to Sonos** | Serves a track *off the SD card* over HTTP and hands Sonos the URL. Your file, their speaker. |
| **On-device speaker** | Plays the SD card track through the board's own speaker (SD → MP3 decode → ES8311). |

All three loop until you stop them.

### Now Playing

The track name, a **Stop** button, and a volume row — **`[−]` slider `[+]`**, 2% per tap. The
volume always drives whatever is actually playing: the Sonos speaker's volume in the two Sonos
modes, the on-board codec in device mode.

Also here: the **Wake** button.

### Wake

Swaps whatever is playing for your **wake track**, *on the output that's already in use* — if
it's on Sonos it stays on Sonos, if it's on the device speaker it stays there. The volume is
left exactly where it is, and the wake track loops until you hit Stop.

The button only appears when there's actually something to switch to. It hides when the card has
no wake track, and once the wake track is already playing.

If you never pick a wake track, the device uses **any file named `wake`** (case-insensitive), so
dropping a `Wake.mp3` on the card is enough to make the button appear.

### Sleep timer and screensaver

While playing, the screen dims to a screensaver, and a sleep timer runs.

### Settings

Brightness · Sonos room · **Sleep Track** · **Wake Track** · Wi-Fi · Device name · **File
Manager**.

- **Sleep / Wake Track** open the same picker, listing the MP3s on the card.
- **Wi-Fi** — this unit *does* have on-device WiFi setup (network list + on-screen keyboard),
  unlike the nest.
- **Device name** sets the network hostname.
- **File Manager** is read-only: it shows the URL of the device's own web page (see below), so
  you don't have to go hunting for its IP address.

---

## Setup

### 1. Prepare the SD card

Put some MP3s on a microSD card and insert it. Anything works, but the two useful conventions:

- A file named **`Wake.mp3`** is auto-detected as the wake track.
- Long tracks are fine — the sleep sounds in use are ~1 hour, ~87 MB each.

Only **MP3** is supported, and only in the **card's root directory** (no subfolders). The
decoder handles baseline MP3.

You can also add files later over WiFi, without removing the card — see *Managing it* below.

### 2. Credentials

```bash
cp include/secrets.example.h include/secrets.h
```

`WIFI_SSID` / `WIFI_PASS` are optional — set them to bake WiFi in at flash time, or leave them
blank and provision on first boot: with no stored credentials the unit raises a **SoftAP captive
portal** (`sonos-sleep-setup`), joinable from your phone. You can also change networks any time
from the on-device **Wi-Fi** screen. Set `OTA_PASSWORD` too — it's required for wireless flashing,
and it has to be flashed *once over USB* before OTA will work.

### 3. Build and flash over USB

```bash
export PATH="$PATH:$HOME/.platformio/penv/bin"
pio run -e sleep-machine
pio run -e sleep-machine -t upload --upload-port /dev/ttyACM0
```

First flash needs download mode: hold **BOOT**, tap **RST**, release **BOOT**. On WSL see
[`flashing-wsl.md`](flashing-wsl.md) — and note the port number bumps on every reset, so resolve
it dynamically with `--upload-port "$(ls /dev/ttyACM* | head -1)"`.

### 4. First boot

It joins WiFi, discovers Sonos, and picks a room. Set the room you want in **Settings → Sonos
room** (or from the web page). Then pick your **Sleep Track** and **Wake Track**.

---

## Managing it

### The web page — add and remove tracks over WiFi

The device serves its own management page. Get the address from **Settings → File Manager** on
the device (it's `http://<device-ip>:8080`), and open it in any browser on the same network.

From there you can:

- **See what's on the card** — track names, sizes, and how full it is.
- **Upload** an MP3 — drag and drop, or click to choose.
- **Delete** a track.
- **Choose the Sleep Track, the Wake Track, and the Sonos room** — the same settings as on the
  device, changeable from the couch.

Two behaviours worth knowing:

**Uploads and deletes are refused while something is playing.** The audio path reads MP3s off
the same card, and writing to it underneath playback glitches the sound. Stop playback on the
device first — the web page greys the buttons out and tells you.

**Deleting a track that was selected clears the selection**, rather than leaving the setting
pointing at a file that no longer exists. Wake falls back to auto-detect, sleep to the default.

**Upload speed** is limited by the SD card itself (~180 KB/s write on the current card), not by
WiFi. A few-minute track lands in under a minute; an hour-long, ~87 MB one takes ~14 minutes.
If that bothers you, a faster (A1-rated) card is the fix — the firmware isn't the bottleneck.

### Wireless updates (OTA)

```bash
pio run -e sleep-machine-ota -t upload --upload-port <device-ip>
```

The unit advertises as **`sonos-sleep`** — a different name from the nest's `sonos-nest`, so the
two never collide. A failed transfer is harmless; the running firmware is untouched.

With Claude Code in this repo, the **`/ota` skill** handles the firewall check, device lookup and
password automatically.

### Troubleshooting

| Symptom | Cause |
|---|---|
| Web page won't load | Check the IP on **Settings → File Manager**; DHCP may have moved it. |
| Upload fails immediately | Something's playing — stop it on the device. |
| Upload crawls | The SD card's write speed, not the network. Expected. |
| Wake button missing | No wake track on the card, or wake is already what's playing. |
| No sound on the device speaker | First tap lazily mounts the SD and brings up the codec, so it can pause briefly; check a card is seated. |
| Sonos volume changes but the track doesn't | In a group, **transport goes to the coordinator, volume is per-speaker**. |
| Photo-negative colours | The ILI9341V on this board needs colour inversion on (`LCD_INVERT_COLORS`). |
