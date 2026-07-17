# hardware/ — sonos-nest physical builds

3D-printed mounts and enclosures for the standalone Sonos controllers. Each unit gets
its own directory: a main dir with the board/mechanical spec, and a sub-dir per part.

All parts are generated with a Python CSG toolchain (trimesh + manifold3d), not
OpenSCAD — each part dir has `stand_params.py` / `mount_params.py` shared params,
`build_*.py` generators, a `render_preview.py`, and the resulting `.stl` files. On this
machine the toolchain lives in the `img23d` conda env
(`conda run -n img23d python build_all.py`).

| unit | board / display | parts |
|------|-----------------|-------|
| **[round-nest-2.8/](round-nest-2.8/)** | ELECROW CrowPanel **2.1"** ESP32-S3 rotary display (round, Ø79 rotating bezel) | **[wall/](round-nest-2.8/wall/)** — magnetic pull-off wall mount |
| **[rec-2.8/](rec-2.8/)** | Hosyond / LCDWIKI **ES3C28P 2.8"** ESP32-S3 board (microSD, rectangular) | **[countertop/](rec-2.8/countertop/)** — angled nightstand stand |
| **[cam-button/](cam-button/)** | nulllab / emakefun **ESP32-S3-CAM** + one FILN FLM12-FJ-6 button (headless) | **[shell/](cam-button/shell/)** — box taped under a nightstand, button facing down |

> Note: `round-nest-2.8` holds the **2.1"** round unit — the `2.8` is a chosen folder
> name, not the display size. See that dir's README.

## Conventions

- **Mount the round unit from the rear only** — its front bezel rotates; never clamp it.
- Keep `hardware/` commits **separate from firmware**; this is user-owned work.
- Board outlines/holes come from verified spec drawings; connector/port positions that
  aren't dimensioned on those drawings are estimated from photos and flagged
  **VERIFY** in the part READMEs — caliper-check before a final print.
