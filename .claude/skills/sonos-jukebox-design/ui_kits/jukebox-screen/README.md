# Jukebox Screen — UI Kit

Interactive recreation of the on-glass UI for the Sonos Jukebox 7" controller,
framed inside the physical device (matte face + rotary dial + transport buttons).

## Run
Open `index.html`. Loads the compiled design-system bundle (`../../_ds_bundle.js`),
Lucide icons (CDN), and the screens below.

## Interactions (fake data)
- **Left rail** (touch) — switch Now Playing / Radio / Rooms; the bottom rail button cycles the album-art layout (split / hero / full-bleed).
- **Physical dial** — click or scroll to change volume (shows the volume overlay); represents push-to-select + turn.
- **Play/Pause** (accent button) — toggles playback. **Skip back / forward** on the bottom row. **Room button** opens the room picker.
- **Radio list** — tap a station to start it and jump to Now Playing.
- **Rooms** — tap the ✓ to add/remove a room from the group; per-room `−`/`+` sets volume and each row has its own play/pause. The summary bar shows group volume, Ungroup, and a group play/pause.

## Files
- `data.js` — sample rooms, track, and stations (window.JB).
- `NowPlaying.jsx` — art, metadata, scrubber, transport, volume.
- `RadioBrowser.jsx` — genre tabs + station ListRows.
- `RoomPicker.jsx` — group toggles, per-room volume + play/pause, group summary bar.
- `App.jsx` — device shell, navigation, volume overlay.

Composes the system primitives — StatusBar, TransportButton, RoomChip, Dial,
ListRow, Scrubber, VolumeBar, Badge — never reimplements them.
