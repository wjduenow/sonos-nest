# Handoff: Sonos Jukebox — Design System & Screen UI

> **For Claude Code:** unzip this project, then invoke the skill defined in `SKILL.md`.
> This file is the implementation brief; `readme.md` is the full design guide.

## Overview
**Sonos Jukebox** is a wall-mounted hardware controller for a Sonos whole-home audio setup: an
**ESP32-S3** board driving a **7" 1024×600 IPS touchscreen**, in a 3D-printed, flush-wall-mounted
case. Physical controls are one **large rotary dial** (turn = volume / list scroll, push = select)
and **four momentary push-buttons**: play/pause, skip forward, skip back, change rooms. Power is
**USB-C**, routed by a thin pigtail to a **rear-mounted female USB-C** connector in the wall cavity.

This project contains both halves of the design: the **industrial design** of the physical unit and
the **on-glass UI** (the screen software — Now Playing, Radio, Rooms).

## About the Design Files
These are **design references created in HTML** — prototypes showing intended look and behavior,
**not production code to copy directly**. The task is to **recreate these designs in the target
codebase's environment** using its established patterns and libraries. For this product the firmware
UI will likely be **LVGL/C++ on ESP32-S3** (or ESP-IDF + a UI toolkit); the JSX here exists to
specify layout, spacing, color, and behavior precisely — port the *values and behavior*, not the React.

The `industrial/*.html` files are **CSS technical drawings**, not manufacturing files. They specify
proportions and dimensions for a CAD model; they are not STLs and are not tolerance-checked.

## Fidelity
**High-fidelity** for the on-glass UI — final colors, typography, spacing, and interactions;
recreate pixel-accurately at 1024×600.
**Medium-fidelity** for the industrial design — dimensions are proposals scaled from the 7" panel,
**not yet validated against a real board footprint, encoder/button hardware, or printer tolerances**.
Treat all mm values as a starting point for CAD, not final.

---

## Screens / Views
All screens render inside a **1024×600** frame with a **66px left nav rail**. Every screen root uses
`box-sizing: border-box` with `height: 100%` — this matters; without it padding pushes content past
the 600px panel and clips the transport row.

### Nav rail (persistent, 66px wide)
Right border `1px solid var(--screen-line)`. Four 48×48 buttons, `border-radius: 14px`, `gap: 10px`,
`padding-top: 22px`. Active = accent text on `color-mix(in oklch, var(--accent) 18%, transparent)`;
inactive = `var(--screen-text-dim)`, transparent.
Items: `disc-3` (Now), `radio` (Radio), `speaker` (Rooms), and a bottom-aligned layout-cycle button
(`margin-top: auto; margin-bottom: 18px`, `1px solid var(--screen-line)`) whose glyph reflects the
current art layout (`columns-2` / `square` / `maximize`).

### 1. Now Playing — three art-prominence layouts
Purpose: see and control what's playing. Cycled by the bottom rail button.

**a) Split (default)** — padding `22px 30px 18px`.
Top: StatusBar. Middle (`flex:1; min-height:0; overflow:hidden`): a **268×268** art tile,
`border-radius: 20px`, `box-shadow: 0 24px 60px rgba(0,0,0,.5)`, with an inset
`0 0 0 1px rgba(255,255,255,.08)` hairline; `gap: 34px` to the text column.
Text column: source + quality Badges (`gap: 8px`, `margin-bottom: 16px`), title at
**800 52px/1.02, letter-spacing -.02em**, artist · album at **500 22px/1.3** in `--screen-text-mut`
(`margin-top: 8px`), then Scrubber at `margin-top: 28px`.
Bottom row: VolumeBar (flex:1) + transport cluster (`gap: 22px` between; `gap: 14px` within).

**b) Hero** — art enlarged to **300×300**, centred with the text column (`gap: 30px`, max-width 360px).
Title **800 44px/1.02**, artist **500 20px/1.3**, album **400 16px/1.3** in `--screen-text-dim`.

**c) Full-bleed** — art fills the panel; over it a protection gradient:
`linear-gradient(180deg, rgba(8,9,11,.72) 0%, rgba(8,9,11,.18) 32%, rgba(8,9,11,.86) 78%, rgba(8,9,11,.96) 100%)`.
Title **800 56px/1**, `text-shadow: 0 2px 24px rgba(0,0,0,.5)`; artist **500 24px/1.3**. Padding `22px 34px 20px`.

**Live radio:** when `duration === 0` the Scrubber must receive `live` — the bar fills, the handle is
hidden, and it reads `LIVE · on air`. Never fabricate a duration for live streams.

### 2. Radio browser
Purpose: pick a station. Padding `22px 30px 12px`. Title "Radio" at **800 30px/1, -.02em**.
Genre tabs (Featured / Music / Talk / Local / Podcasts) — **600 13px/1**, `padding: 8px 14px`,
pill radius; active is accent text on `color-mix(in oklch, var(--accent) 16%, transparent)`.
Scrolling list of ListRows (`gap: 2px`), each with a LIVE Badge. Tapping a station starts it and
jumps to Now Playing.

### 3. Rooms — grouping, volume, playback
Purpose: manage which speakers play and at what level. Padding `22px 30px 16px`.

**Group summary bar** — `border-radius: var(--r-lg)`, `background: var(--screen-elev)`,
`1px solid var(--screen-line)`, `padding: 14px 18px`, `gap: 18px`, `margin: 12px 0 14px`.
Left: room count ("3 rooms") at **800 24px/1, -.02em**, member list beneath at **400 13px/1.3** in
`--screen-text-dim`. Middle: "GROUP VOLUME" mono caps label + VolumeBar showing the **average** of
grouped rooms. Right: an UNGROUP button (mono caps, height 40) and a group play/pause TransportButton.

**Room rows** — `padding: 12px 16px`, `border-radius: var(--r-md)`, `gap: 16px`.
In-group rows get `background: var(--screen-elev)` and an inset hairline; the **active** room gets
`inset 0 0 0 1px var(--accent)` and a `MAIN` badge.
- **Group toggle**: 26×26, `border-radius: 8px`. Checked = accent fill + `check` glyph; unchecked =
  `1.5px solid var(--screen-text-dim)`, transparent.
- **Name block** (180px): name at **600 17px/1.2**, caption at **400 12px/1.3**.
  Caption logic: `Idle` when not playing; `Playing · grouped` **only when the group has >1 member**;
  otherwise `Playing`.
- **Volume**: VolumeBar, `opacity: .45` when the room is idle.
- **Controls**: `−` / `+` steppers (32×32, `border-radius: 10px`, `1px solid var(--screen-line)`,
  `background: var(--screen-elev-2)`, ±5 per press) and a per-room play/pause (`sm`, solid when playing).

---

## Interactions & Behavior
- **Rail** switches views; the 4th button cycles art layout split → hero → bleed and forces the Now view.
- **Physical dial** (in the prototype: click or scroll) changes volume and shows a **modal volume
  overlay** — full-screen `rgba(8,9,11,.55)` scrim with `backdrop-filter: blur(3px)` and a centred
  220px `Dial`, auto-dismissing after **1400ms** (timer resets on each turn).
- **Play/pause** toggles playback; **change rooms** opens the Rooms view.
- **Station tap** sets the track, marks the source RADIO, starts playback, and navigates to Now Playing.
- **Group toggle** adds/removes a room; joining inherits the main room's playing state. The **active
  room cannot be removed** from its own group.
- **UNGROUP** resets the group to just the active room and stops the others.
- **Transitions**: `.12–.15s ease` on press/hover only. No bounces, no looping or decorative motion.

## State Management
Single state object in `ui_kits/jukebox-screen/App.jsx`:
- `view` `'now'|'radio'|'rooms'` · `artLayout` `'split'|'hero'|'bleed'`
- `playing`, `shuffle`, `source`, `stationId`, `track` `{title, artist, album, elapsed, duration, art, quality}`
  (`duration: 0` ⇒ live)
- `activeRoom` (id), `grouped` (array of ids), `rooms` `[{id, name, vol, playing}]`
- `vol`, `volShow` (overlay visibility, with a dismiss timer ref)

Real implementation needs the Sonos local control API (or node-sonos-http-api) for discovery,
grouping, transport, and volume, plus a rotary-encoder ISR and debounced button GPIO reads.

## Design Tokens
Full source in `tokens/` (`styles.css` imports them all). Key values:

**Screen (on-glass):** bg `#0e0f12` · elev `#191b20` · elev-2 `#23262d` · line `#2c3038` ·
text `#f4f5f7` · text-mut `#9aa0ab` · text-dim `#6a7079`
**Accent:** amber `#e8892b` (live) — soft `#f7c893`, deep `#c46a17`. Alternates: teal `#2fa5a0`,
coral `#f0605a`. One accent at a time.
**Neutral/physical:** ink-900 `#16181c` · ink-700 `#3a3f47` · ink-500 `#6b7280` · ink-300 `#b8bdc4` ·
line `#e6e8ec` · surface-0 `#ffffff` · surface-1 `#f6f7f9` · surface-2 `#eef0f3`
**Materials:** case `#f2f2ee` · knob `#2a2c31` (hi `#4a4d55`) · button `#f7f7f4` · glass `#0c0d10`
**Spacing:** 4px grid — 4·8·12·16·20·24·32·40·48·64. Min touch target **44px**.
**Radii:** 6 · 10 · 14 (rows/chips) · 20 (cards) · 28 (pages) · pill. Physical case corner 6mm.
**Type:** Hanken Grotesk 400–800; JetBrains Mono for spec/metadata, uppercase, `letter-spacing: .14em`.
Scale: 44 / 30 / 24 / 20 / 17 / 15 / 13 / 11.
**Shadows:** `--shadow-md 0 4px 12px rgba(22,24,28,.08), 0 1px 3px rgba(22,24,28,.05)`;
`--shadow-lg 0 18px 40px rgba(22,24,28,.12), 0 4px 10px rgba(22,24,28,.06)`;
`--inset-well inset 0 2px 6px rgba(22,24,28,.18), inset 0 -1px 1px rgba(255,255,255,.6)`.

**Hardware (`tokens/hardware.css`, proposals):** face 210 × 130 × 22 mm · screen 154 × 86 mm ·
bezel 9mm · control column 46mm · dial Ø36mm, 14mm proud · buttons Ø13mm at 9mm pitch ·
case corner radius 6mm · wall plate 3mm proud · rear USB-C receptacle 9mm.

## Assets
- **Icons: [Lucide](https://lucide.dev) via CDN (`lucide@0.462.0`)** — a **substitution**; no brand
  icon set was supplied. Used: `play`, `pause`, `skip-forward`, `skip-back`, `chevron-left`,
  `arrow-left-right`, `shuffle`, `wifi`, `volume-1/2/x`, `audio-lines`, `disc-3`, `radio`, `speaker`,
  `check`, `plus`, `minus`, `columns-2`, `square`, `maximize`.
- **Fonts: Hanken Grotesk + JetBrains Mono via Google Fonts** — also a **substitution**; no brand font
  files were supplied. Replace with licensed `@font-face` files if a brand face is chosen.
- **No logo.** None was supplied; the product name is set in plain type. **Do not create a Sonos
  logo or mark** — Sonos is a third party; "Jukebox" is the user's own controller product.
- Album/station art is placeholder CSS gradients; real art comes from the Sonos API.

⚠️ **React + icons:** render glyphs with the system's `Icon` component. **Never call
`lucide.createIcons()`** — that global scan replaces React-owned nodes with raw SVG and crashes the
tree when an icon is conditionally rendered or swapped.

## Files
- `readme.md` — full design guide (content fundamentals, visual foundations, iconography).
- `SKILL.md` — Agent Skill wrapper; invoke this from Claude Code.
- `styles.css` + `tokens/` — the token layer; link `styles.css` to get everything.
- `industrial/` — `front-view`, `side-view` (mount + USB-C routing), `exploded`, `control-layout`,
  `variations` (accent options).
- `components/` — `controls/` (TransportButton, RoomChip, Dial), `display/` (ListRow, Scrubber,
  VolumeBar), `system/` (Badge, StatusBar, Icon). Each has a `.d.ts` props contract and a
  `.prompt.md` usage note.
- `ui_kits/jukebox-screen/` — **open `index.html` to see the working prototype.**
  `App.jsx` (shell, state, dial, overlay), `NowPlaying.jsx`, `RadioBrowser.jsx`, `RoomPicker.jsx`, `data.js`.
- `_ds_bundle.js` — compiled components, exposed as `window.SonosJukeboxDesignSystem_e55a41`.

## Open questions for the developer / designer
1. Accent not finalized — amber, teal, or coral (see `industrial/variations.html`).
2. All mm dimensions need validation against the real board, panel, encoder, and button hardware
   before CAD; mounting-hole positions are not yet specified.
3. Encoder shaft diameter and button barrel/bushing sizes drive the front-plate cutouts — unspecified.
