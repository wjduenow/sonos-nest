---
name: sonos-jukebox-design
description: Use this skill to generate well-branded interfaces and assets for Sonos Jukebox — a wall-mounted ESP32-S3 hardware controller for Sonos (5"/7" touchscreen, rotary dial + transport buttons, 3D-printed case) — either for production or throwaway prototypes/mocks. Contains industrial-design specs, colors, type, fonts, assets, and on-glass UI kit components for prototyping both the physical unit and its screen software.
user-invocable: true
---

Read the `readme.md` file within this skill, and explore the other available files.

If creating visual artifacts (industrial-design views, screen mocks, throwaway prototypes, etc.),
copy assets out and create static HTML files for the user to view. If working on production code,
you can copy assets and read the rules here to become an expert in designing with this brand.

Two worlds share one language:
- **Physical** — matte-white case, warm diffuse shadows, mm dimensions in `tokens/hardware.css`,
  CSS technical drawings under `industrial/`.
- **On-glass** — near-black screen UI, single accent (amber, with teal/coral alternates), Lucide
  icons, primitives under `components/` composed in `ui_kits/jukebox-screen/`.

Link `styles.css` for tokens/fonts; load `_ds_bundle.js` and use `window.SonosJukeboxDesignSystem_*`
for components. Load the Lucide UMD script and render glyphs via the `Icon` component — never call
`lucide.createIcons()`.

If the user invokes this skill without any other guidance, ask them what they want to build or design,
ask some questions, and act as an expert designer who outputs HTML artifacts _or_ production code,
depending on the need.

---

## In this repo (added on import — not part of the upstream design project)

Imported from the Claude Design project
`https://claude.ai/design/p/e55a4165-1f93-46dd-816c-66c679c9a2e6` ("Sonos Jukebox Design System").
Everything above this line is upstream; everything below is sonos-nest-specific.

**Re-synced from an updated handoff (`HANDOFF.md`, 2026-08-08).** The big change is **Rooms**:
`ui_kits/jukebox-screen/RoomPicker.jsx` went from a grid of RoomChips to full-width rows with a
group summary bar (count · members · average GROUP VOLUME · UNGROUP · group play/pause) and, per
row, a 26×26 group checkbox, name + state caption, volume bar, ±5 steppers and a play/pause.
`components/system/Icon.*` is new. The firmware still implements the OLD chip grid.

> ⚠️ **The handoff's hardware description is stale — trust the repo, not `HANDOFF.md`.** It says
> "ESP32-S3" and "four momentary push-buttons"; the real unit is an **ESP32-P4** with **one**
> Modulino dial and **no** buttons (they were dropped — see the `hardware/jukebox-7` log). Its mm
> dimensions are also explicitly unvalidated proposals. The *on-glass* spec is the authoritative
> part; the industrial half is not.

**The React/HTML here is a specification, not shippable code.** The device runs LVGL 9 on an
ESP32-P4 — there is no DOM, no CSS, no Lucide. Use these files as the source of truth for colors,
type, spacing, radii, and layout, and translate them into LVGL styles under
`src/units/sonos_jukebox/`. The HTML cards are for *previewing* a design in a browser before
committing it to firmware.

Translation notes when this becomes firmware:
- Tokens → an LVGL palette/style header in the unit (mirror `tokens/colors.css` names so the two
  stay comparable). `color-mix()` tints have no LVGL equivalent — pre-compute them.
- Lucide glyphs → an LVGL font subset or image assets; there is no icon font on the device yet.
- Hanken Grotesk / JetBrains Mono are **substitutions** upstream; on-device they become converted
  LVGL fonts at the specific sizes in `tokens/typography.css` (each size costs flash — subset them).
- `tokens/hardware.css` is the case's dimension source of truth; `hardware/` in this repo is where
  the printable parts live (see the repo CLAUDE.md — hardware commits stay separate from firmware).
- The physical control set the board ACTUALLY exposes through `core/board.h` is one push-select
  dial (`knobEvent`/`encoderDelta`) — the four transport caps in the upstream drawings do not
  exist on the built unit, so no screen may depend on them.
