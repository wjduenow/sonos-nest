# round-nest-2.8 — original round Sonos controller (hardware)

Hardware for the **original** standalone Sonos knob controller: the **ELECROW
CrowPanel 2.1" HMI ESP32-S3 Rotary Display** (round body, Ø79 rotating front bezel).

> **Naming note:** the display is physically **2.1"** round. The `-2.8` in this folder
> name is kept as requested — rename to `round-nest-2.1` if that was unintended.

## Contents

- **`crowpanel-2.1-physical-spec.md`** — reverse-engineered mechanical spec (Ø79
  rotating bezel, Ø58 rear body, 3×M3 Ø12-BC rear holes, **4-pin MX1.25 JST on the back
  — not USB-C**). Read this before designing any part for this unit. **Hard rule: mount
  from the rear only — the front bezel rotates, never clamp it.**
- **`wall/`** — the magnetic pull-off **wall mount** (wall plate + cradle, disc magnets,
  anti-rotation key, 1:1 paper drill templates).

See `wall/README.md` for the wall-mount build and assembly.
