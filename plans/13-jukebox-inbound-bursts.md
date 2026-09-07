# 13 — The jukebox link death: shrink the inbound bursts

Status: **candidate 1 built, not yet measured** — tracked as
[issue #24](https://github.com/wjduenow/sonos-nest/issues/24), branch `fix/jukebox-inbound-bursts`.
Started 2026-09-07 as a handoff stub at the end of the plans/12 work, so the next session starts
from evidence instead of memory.

## What is known (all measured on hardware, 2026-09-07)

- The ESP-Hosted SDIO link between the P4 and the C6 dies under inbound bursts. Four deaths in
  four runs of the same sequence; the last at **Play All**, with the browse and its tiles already
  finished. The diary read `netlink:fast dead=7s gapmax=5s@linkstats` each time.
- Upstream esp-hosted-mcu **#184** names the mechanism: inbound flow control — once the C6's Wi-Fi
  RX buffers fill with inbound TCP, the SDIO host mishandles the backpressure and the link freezes.
  #167 / #121 are the same fault. All open, no maintainer response, no fix.
- **Every transport-side lead is closed.** SDIO is already 1-bit at 10 MHz (#167 says 20 MHz did
  not help). Packet mode instead of stream mode is a hard assert at init (boot loop, USB recovery).
  The C6 already runs slave **2.12.11**, matching the host library exactly (`jukebox-c6` probe).
- The largest inbound transfer the panel makes is the Now Playing cover at play time: the speaker
  serves a Spotify cover through `/getaa` as **158 KB, chunked, in 0.39 s** — full LAN speed into
  the C6. `getaa` has no rendition knob (`s=`, `&v=`, `&size=` are byte-identical). Tiles already
  dodge this by asking Spotify's CDN for small renditions; Now Playing cannot, because all it has
  is the `getaa` URL.
- Detection and recovery are now ~7 s + boot, with a reboot note that explains each death
  (`core/app.cpp` `deadLinkFast()`, `health.lastReboot`). The `[art] N B in M ms` log line lands
  right before a death, so correlation is one grep.

## Candidate designs (pick by measurement, not preference)

1. **Resolve Spotify Now Playing art through SMAPI and fetch a small rendition from the CDN.**
   The `getaa` URL carries the track id (`x-sonos-spotify:spotify%3atrack%3a<id>`); one small
   SMAPI `getMediaMetadata` call returns the CDN `albumArtURI` with the image hash, and the 300 px
   rendition (`ab67616d00001e02…`) is ~20-30 KB instead of 158 KB. Reuses `core/spotify` and the
   rendition rules already in `art_cache.cpp::thumbUrl()`. Only helps Spotify content — but that
   is where every death so far has been.
2. **Pace the read.** Cap what the sender can have in flight so the C6's RX buffers never fill:
   a per-socket receive window (lwIP `TCP_WND` is global and the buffer experiment is documented
   as harmful; a per-pcb window via `setsockopt(SO_RCVBUF)` is untested here). #184's reporter
   tried 5760 and 32768 windows without relief — treat this as the weaker lead.
3. **Fetch tiles only for the viewport.** Search paints up to 8 rows and a drill-down up to 24
   while the list shows five; `artcache::get()` queues a fetch for every miss, so a single paint
   can enqueue 24 TLS fetches, paced 120 ms apart. Gate `srchPaintArt()` to the visible rows and
   repaint on scroll (the Amazon Radio list has the same shape). Raised in review on PR #23.
4. **Do nothing at play time.** Defer the Now Playing fetch until the GENA notify and the poll are
   quiet, and never overlap it with a tile fetch or a browse (extend the `smapi::busy()` rule to a
   single "one inbound transfer at a time" gate across art, tiles, browse and the crawl).

## Method

- Baseline first: the reproduction is Radio → Spotify → (any playlist or album) → Play All, and it
  has died 4 for 4. Run it five times on the current build and count. Anything that does not move
  that number is not a fix.
- One change per flash. Watch `[art]` bytes and `health.lastReboot` on each run.
- Nothing touching the transport goes over the air (plans/07 and the packet-mode note in
  `platformio.ini`).

## Candidate 1 — as built (2026-09-07, awaiting the five-run measurement)

- `spotify::trackIdFromSonosUri()` pulls the track id out of the `/getaa` URL. The id sits in the
  `u=` parameter percent-encoded **twice** (`spotify%253atrack%253a<id>`), so the extractor matches
  the separator at every encoding depth, longest first.
- `spotify::trackArtUrl(id, px)` is one `getMediaMetadata` call on the pooled SMAPI session, parsed
  for `albumArtURI`, with the album-cover rendition prefix rewritten `b273` → `1e02` (640 → 300 px)
  for a cap of 300 or under. One-entry cache, because artTask retries a track up to four times and
  GENA + the poll can each republish the same `artUri`. "" on anything short of an https answer.
- `albumArtFetch()` (jukebox only — `ALBUM_ART_TLS`) resolves any `/getaa?` URL that carries a
  Spotify track id and fetches the CDN URL over TLS instead; every failure falls through to `/getaa`
  unchanged, with a log line saying so. It also now refuses a non-JPEG `Content-Type` at the header,
  the same guard the tile fetcher has.
- The size log line names its path: `[art] N B in M ms via cdn` or `via getaa`. Correlate against
  `health.lastReboot` per the method above — a death that follows a `via cdn` line at ~18 KB would
  mean the cover was not the burst that matters.
- Expected on the wire: ~158 KB chunked from the speaker → ~18 KB from `i.scdn.co`, plus a ~1-2 KB
  SOAP round trip. Decoded size goes 320 px (640/2) → 300 px (300/1) under the 320 cap; visually
  the same tile.
- Not covered: content the token cannot see (an unlinked device, or a track SMAPI has no art for),
  Amazon/TuneIn covers through `/getaa` (those were not the deaths), and the 158 KB the speaker
  still serves to anything else that asks. Candidates 2-4 stay open until the count says otherwise.
