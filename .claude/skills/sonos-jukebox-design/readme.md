# Sonos Jukebox — Design System

A design system for **Sonos Jukebox**, a wall-mounted hardware controller for a Sonos
whole-home audio setup. The unit is an **ESP32-S3** board driving a **7" IPS touchscreen** (1024×600; a 5" 800×480 variant
shares the same design language),
housed in a 3D-printed, wall-mountable case with **generously rounded corners** and physical controls:
a **large rotary dial** (volume + list scroll, push-to-select), and four dedicated momentary push-buttons —
**play/pause**, **skip forward**, **skip back**, and **change rooms**, laid out in a 2×2 grid below the dial.
Power is **USB-C**, routed via a thin pigtail to a **rear-mounted female USB-C** connector inside the
wall cavity. It is used to control now-playing, browse radio stations, and switch/group rooms.

This system leads with **industrial design** (the physical unit) and pairs it with the **on-glass UI**
(the screen software) so both worlds share one visual language.

> **Note — this is a from-scratch product with no supplied brand assets.** There is **no logo**: the
> product name is set in plain type wherever a mark would go. Do not fabricate a Sonos logo or mark —
> Sonos is a third party; "Jukebox" is the user's own controller product.

## Sources
No codebase or Figma was provided. Direction came from the user's brief and one hardware reference:
- **Dev board reference:** an ESP32 display board (ESP32-32E, ST7796S, 320×480) shared via an Amazon
  listing — used only to confirm form factor and that power is USB-C to a rear female connector. The
  shipping SKUs target larger panels (5" 800×480 and 7" 1024×600).
- All dimensions, colors, type, and UI are original to this system.

## Font substitution ⚠️
No brand font files were supplied. The system uses the closest **Google Fonts** matches, loaded via
CDN in `tokens/fonts.css`:
- **Hanken Grotesk** — clean neutral grotesque for UI + display.
- **JetBrains Mono** — technical mono for dimension callouts and spec labels.
Swap these for licensed `@font-face` files when available (the compiler will then report real fonts).

---

## CONTENT FUNDAMENTALS
How copy is written across the screen UI and product surfaces.

- **Voice:** calm, spatial, and literal — it names rooms and what's playing, nothing more.
  "Kitchen +1", "Now: Lauren Laverne", "Grouped with Bedroom". Never chatty, never salesy.
- **Person:** neutral/system voice. Avoids "I"; addresses the user implicitly ("Rooms", "Radio")
  rather than "Your rooms". Labels are nouns, actions are verbs ("Group", "Play").
- **Casing:** **Title case** for view titles and room names (Now Playing, Living Room). **Uppercase
  mono** for metadata tags and spec labels, tightly tracked (LIVE, FLAC · 44.1, Ø36 DIAL, 210 × 130 × 22 MM).
  Sentence case for any longer helper text.
- **Numbers & units:** always explicit and metric on hardware (mm, Ø for diameter). Timecodes are
  mono `m:ss` with a leading `-` for remaining. Bitrate/quality shown as `FLAC · 44.1` / `AAC · 128`.
- **Punctuation:** middot `·` separates peer metadata (artist · album · quality). Real Unicode
  glyphs (—, ·, Ø, →), never HTML entities.
- **Emoji:** none. This is an appliance, not an app store.
- **Vibe:** quiet, precise, a little audiophile. Think a good amplifier's front panel — labels earn
  their place; whitespace does the rest.

## VISUAL FOUNDATIONS
Two coordinated worlds: the **matte-white physical** unit and the **near-black on-glass** UI.

- **Color:** the whole product lives on a neutral ink→surface ramp (`--ink-900` … `--surface-0`).
  The physical case is warm matte white; the screen UI sits on near-black (`--screen-bg #0e0f12`) for
  OLED-like contrast. A single **accent** carries brand + interaction — currently **amber** (`--amber`),
  with **teal** and **coral** as explorable alternates (see the Accent trim card). One accent at a
  time; it appears as the dial light-ring, progress fill, active states, and status dot.
- **Type:** Hanken Grotesk. Display is heavy (800) and tightly tracked (`-0.02em`); body is 400–600.
  Mono (JetBrains) is reserved for spec/metadata and is uppercase + widely tracked (`0.14em`).
- **Spacing:** 4px base grid (`--sp-*`). On-glass gutters 18–28px by SKU. Physical dimensions live in
  `tokens/hardware.css` in millimetres.
- **Backgrounds:** flat, no imagery or gradients on the case beyond a subtle top-light sheen; the
  screen UI is flat near-black. Album/station art is the only "image" surface — shown as rounded
  tiles (solid color or gradient placeholder here; real art in production).
- **Radii:** soft, Sonos-adjacent. `--r-md 14` rows/chips, `--r-lg 20` cards, `--r-xl 28` page
  containers; physical case corner ~6mm. Controls are fully round (`--r-pill`).
- **Shadows:** two systems. **Physical** = warm, diffuse, multi-layer (`--shadow-md/lg/float`) plus
  recessed insets for the screen well and button seats (`--inset-well`). **On-glass** = restrained;
  elevation is mostly a lighter fill + hairline (`--screen-line`), with one soft drop for overlays.
- **Borders:** hairlines only — `--line` on white, `--screen-line` on black. No heavy strokes.
- **Animation:** minimal and physical. Short eases (`.12–.15s`) for press/hover; volume overlay fades
  in and auto-dismisses (~1.4s). No bounces, no infinite loops, no decorative motion.
- **Hover / press:** hover lightens fill or shifts to accent; press shrinks slightly and deepens.
  On the near-black UI, "hover" is really touch — states favor an accent tint fill over color swaps.
- **Transparency & blur:** used only for the modal volume overlay (dark scrim + slight blur). Elsewhere
  surfaces are opaque.
- **Cards:** white on `--surface-1` with a hairline and `--shadow-md`; on-glass "cards" are
  `--screen-elev` with a hairline, no drop shadow.
- **Imagery vibe:** warm, low-key. Placeholder art skews warm/earthy to sit under amber.

## ICONOGRAPHY
- **Set:** [Lucide](https://lucide.dev) — thin, consistent 2.2px stroke, round caps — matches the soft
  grotesque and the physical control glyphs. Loaded via CDN (`lucide@0.462.0`). Always render glyphs with the
  system's **`Icon`** component — never call `lucide.createIcons()`, which replaces React-owned nodes
  with raw SVG and crashes the tree when an icon is conditionally rendered or swapped. **This is a substitution** (no brand icon set was supplied) but a deliberate, close one.
- **Usage:** transport (`play`, `pause`, `chevron-left`, `skip-forward`, `skip-back`, `refresh-cw`,
  `shuffle`), status/connectivity (`wifi`, `bluetooth`, `cast`), volume (`volume-1/2/x`), rail
  (`disc-3`, `radio`, `speaker`). Physical button caps use the same transport glyphs, engraved.
- **No emoji, no ad-hoc SVG.** The rotary-dial "knurling" and light-ring are CSS (gradients/arcs),
  not iconography. Diagrams in the Industrial Design cards are CSS technical drawings.
- **Assets:** no raster logos/illustrations were provided, so `assets/` is intentionally empty. Add
  a real wordmark/photography here when available.

---

## Components
On-glass UI primitives (namespace `SonosJukeboxDesignSystem_*`). Grouped by concern:

**Controls** (`components/controls/`)
- **TransportButton** — round transport control (play/pause/back/room/skip); ghost / elevated / solid.
- **RoomChip** — selectable room pill with playing + group state.
- **Dial** — on-glass rotary indicator (270° accent arc) mirroring the physical knob; volume overlay.

**Display** (`components/display/`)
- **ListRow** — station / track / room / source row with art tile, meta, trailing slot.
- **Scrubber** — playback progress with mono timecodes; `live` mode for radio.
- **VolumeBar** — inline horizontal level with state-aware speaker icon.

**System** (`components/system/`)
- **StatusBar** — persistent top strip: room + group, connectivity, clock.
- **Badge** — uppercase mono tag for source/quality/state (LIVE, FLAC, SPOTIFY).
- **Icon** — Lucide glyph in a React-safe wrapper; the only correct way to render an icon here.

## Index / manifest
- `styles.css` — global entry (import list only).
- `tokens/` — `fonts.css`, `colors.css`, `typography.css`, `spacing.css`, `radii.css`, `shadows.css`, `hardware.css`.
- `guidelines/` — foundation specimen cards (Colors, Type, Spacing).
- `industrial/` — the physical unit: `front-view`, `side-view`, `exploded`, `control-layout`, `variations`.
- `components/` — reusable primitives (controls / display / system), each with a `@dsCard`.
- `ui_kits/jukebox-screen/` — interactive 7" device recreation.
- `assets/` — logos/imagery (empty — none supplied).
- `SKILL.md` — Agent Skills wrapper.

## Intentional additions
- **Lucide icon dependency** — no brand icon set existed; Lucide is the documented substitute.
- **Icon** — wraps Lucide so React can safely swap/remove glyphs; required because the global
  `createIcons()` scan mutates React-owned DOM.
- **Dial (on-glass)** — added so the software can visualize the physical knob's state (volume/scroll).
