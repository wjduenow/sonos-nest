# Jukebox Screen — UI Kit

Interactive recreation of the on-glass UI for the Sonos Jukebox 7" controller,
framed inside the physical device (matte face + rotary dial + transport buttons).

## Run
Open `index.html`. Loads the compiled design-system bundle (`../../_ds_bundle.js`),
Lucide icons (CDN), and the screens below.

## Interactions (fake data)
- **Left rail** (touch) — switch Now Playing / Radio / Rooms.
- **Physical dial** — click or scroll to change volume (shows the volume overlay); represents push-to-select + turn.
- **Play/Pause** (accent button) — toggles playback.
- **Back button** — returns to Now Playing. **Room button** — opens the room picker.
- **Radio list** — tap a station to start it and jump to Now Playing.
- **Rooms** — tap chips to add rooms to the group; grouped rooms get volume sliders.

## Files
- `data.js` — sample rooms, track, and stations (window.JB).
- `NowPlaying.jsx` — art, metadata, scrubber, transport, volume.
- `RadioBrowser.jsx` — genre tabs + station ListRows.
- `RoomPicker.jsx` — room chips + grouped-room volume.
- `App.jsx` — device shell, navigation, volume overlay.

Composes the system primitives — StatusBar, TransportButton, RoomChip, Dial,
ListRow, Scrubber, VolumeBar, Badge — never reimplements them.
