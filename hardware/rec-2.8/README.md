# rec-2.8 — rectangular 2.8" Sonos controller (hardware)

Hardware for the **second** Sonos controller, built on the **Hosyond / LCDWIKI
ES3C28P** 2.8" ESP32-S3 display board — the variant with a **microSD slot** (for
playing music straight off the card). Rectangular board, meant to sit on a
nightstand / counter top.

## Board — verified spec (QDtech "LCM OUTLINE" drawing, V1.0 2025-06-11)

- PCB **86.0 × 50.0 × 1.6 mm**, corner radius **R3.5**
- **4× Ø3.2** mounting holes on a **42 × 78 mm** rectangle (4 mm in from each edge),
  Ø5.6 keep-out ring each
- front glass proud **4.30 mm**; glass 50 × 69.2; viewing area 43.60 × 58.05;
  active area 43.20 × 57.60; total module thickness **10.60 mm** (back parts ≤ 4.70)
- display **ILI9341V** (4-wire SPI); touch **FT6336G** (I²C @ 0x38); **ESP32-S3R8**
  (8 MB OPI PSRAM, 16 MB flash)
- **USB-C + RESET + BOOT** on one short (50 mm) edge; **microSD** push-push socket
  **mid-board on the back** (SDIO); front **MEMS mic**; addressable **RGB LED on IO42**
- **`ES3C28P_Size.pdf`** — the official dimension drawing (authoritative source for
  outline, holes, glass, thickness)

## Contents

- **`ES3C28P_Size.pdf`** — board mechanical drawing.
- **`countertop/`** — the angled **nightstand stand** case (shell + screwed-on bezel,
  cable channel + clips, RESET pin hole, microSD access, mic port).

See `countertop/README.md` for the stand build. Note: USB-C / RESET / microSD / mic
in-plane positions there are estimated from board photos — **caliper-verify before the
final print**; the outline/holes/glass/thickness above are exact.
