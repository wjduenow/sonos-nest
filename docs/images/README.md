# docs/images — the README's showcase imagery

Everything the top-level [`README.md`](../../README.md) displays lives here. Two kinds of file,
and the difference matters:

| kind | files | source |
|---|---|---|
| **Photographs** (to be taken) | `hero.png`, `unit-*.png` | you, with a camera — currently **placeholders** |
| **Rendered** | `jukebox-*.png` | [`../mockups/render_jukebox_screens.py`](../mockups/render_jukebox_screens.py) |
| **Screenshot** | `portal-dashboard.png` | cropped from `sonos-portal/docs/dashboard.png` |

## Replacing a placeholder

**Overwrite the file at the same path and keep the `.png` name — the README needs no edit.**
The placeholders carry a marker in their PNG metadata, so re-running
`python3 docs/mockups/render_photo_placeholders.py` will *not* clobber a real photo you have
dropped in (use `--force` if you actually want the placeholder back).

A JPEG is fine for photographs — save it as `.png`, or rename the file and fix the one
matching line in `README.md`. PNG for photos is wasteful; if you convert the README to `.jpg`
names, do all six at once so the set stays consistent.

## The shot list

Frame to these aspect ratios; GitHub will scale the width down, so the ratio is what has to be
right. Shoot larger than listed and downscale — 2× is plenty.

| file | ratio | what it has to show |
|---|---|---|
| `hero.png` | **20:9** (2000×900) | **All five units in one frame.** The banner. A wide, shallow crop — group them on one surface, or shoot the wall with the jukebox and nest in it and the two buttons in the foreground. This is the single most load-bearing image in the repo: it is the only one that says "this is a *family* of devices", which is the entire premise. |
| `unit-nest.png` | 4:3 (1200×900) | The round knob **mounted on a wall**, slightly off-axis so the bezel reads as round and the depth is visible. Screen on, showing Now Playing. |
| `unit-sleep-machine.png` | 4:3 | On a nightstand, **screen lit**, in a dim room — it is a sleep device, so a warm low-light frame sells it better than a bright one. |
| `unit-button.png` | 4:3 | In a hand, or thumbed on the underside of a nightstand where it actually lives. **Ring LED lit** — that ring is the entire user interface. |
| `unit-button-v2.png` | 4:3 | **Next to the v1**, same frame, same distance. The whole point of v2 is that it is 2.3× smaller; a photo of it alone conveys nothing, because there is no scale reference in a plain white box. A coin or a thumb works too. |
| `unit-jukebox.png` | 4:3 | The 7" panel **on the wall**, showing a real screen. Glare is expected here and is fine — the *UI* is shown by the rendered screenshots, so this shot only has to establish the physical object in a room. |

### Practical notes

- **Screens photograph badly.** Shoot lit screens at an angle that puts your reflection out of
  frame, in a room dimmer than the panel, with the exposure set for the screen, not the wall.
  If a unit's screen still will not photograph cleanly, say so — the jukebox screens are
  rendered for exactly this reason and the same technique extends to the nest and
  sleep-machine.
- **Backgrounds:** plain and quiet. These are appliances; the design language is matte white
  case, near-black glass, one amber accent. A busy background fights all three.
- **Do not include** anything identifying — a Wi-Fi password on a screen, a house number, a
  face. The README is public.

## Regenerating the rendered images

```bash
python3 docs/mockups/render_jukebox_screens.py      # the four jukebox screens
python3 docs/mockups/render_photo_placeholders.py   # placeholders, skips real photos
```

Both need Pillow, and find Montserrat inside any built PlatformIO LVGL checkout automatically.
The jukebox renderer mirrors coordinates out of `src/units/sonos_jukebox/screens.cpp` — **if
you change that layout, change the renderer too**, or the README starts showing a UI the device
no longer has.
