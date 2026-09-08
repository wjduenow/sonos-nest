# 13 — The jukebox link death: shrink the inbound bursts

Status: **candidates 1 and 4 built, not yet measured** — tracked as
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

## Candidate 4 — as built (2026-09-07, same day; the deaths moved the priority)

Candidate 1 was flashed and the reproduction died at the **browse** step, before Play All was ever
reached: Radio → Spotify → Featured Playlists (12 tiles start) → back to root, and at the next
tile's TLS connect the link was gone. The diary read `netlink:fast dead=7s gapmax=5s@linkstats`, no
`[art]` line anywhere near it. So the Now Playing cover is not the only burst that kills it, and
the plan's candidate 4 went in next rather than waiting for a five-run count of a death that
candidate 1 cannot touch.

- **`core/net/inbound_gate.{h,cpp}`** — one FreeRTOS mutex, one slot, for every large inbound
  reader in core. `inbound::Guard g("tag", maxWaitMs)`; the header has the rules.
- **Holders:** `smapi::Client::post()` (Spotify browse/search/link and the Amazon crawl, 20 s
  wait), `artcache::obtain()` (station tiles, 60 s — background work never gives up and bursts),
  and `albumArtFetch()` (Now Playing art, 10 s — artTask retries). Now Playing takes the gate AFTER
  its SMAPI resolve, because the gate is not recursive, and releases it before decoding.
- **Not gated:** the Sonos SOAP poll, GENA, the registrar, pull-OTA. Reasons in the header.
- **A timed-out wait proceeds ungated and is counted.** `health.inbound` in `/api/config` carries
  `acquires / waits / timeouts / maxWaitMs / maxHeldMs+By / holder+heldMs`. `waits` is the number
  of bursts that did not happen. `timeouts` must stay 0.
- **`smapi::busy()` is gone.** It only ever let tiles wait for a browse; the gate covers every pair
  in both directions.
- **Serial signature to look for:** `[gate] tile waited 1830 ms for spotify` lines around a browse
  where the old build printed a TLS connect timeout and then `RSSI 0`.

## First hardware pass on candidates 1 + 4 (2026-09-07, evening) — three fixes it forced

- **Candidate 1 works, and its first fetch was 167 KB.** `[art] 167572 B in 207 ms via cdn` — the
  CDN path resolved and fetched, but `coverRendition()` only picked the 300 px rendition for a cap
  of ≤300 and the jukebox cap is 320, so it kept the 640 original: 9 KB MORE than `/getaa`. Fixed:
  any cap under 640 takes the 300 px rendition (the decoder would downscale the original to 320 at
  most anyway). Later fetches of a smaller cover read `38716 B` at 8-413 ms.
- **A death took 110 s to detect, not 7.** Diary: `netlink:fast dead=110s gapmax=24s@gena`. The
  detector needed two RSSI-0 sightings within 20 s, but over a dead link every netTask stage blocks
  on its own timeout — SOAP 3+4 s per call, the RSSI RPC 5 s, and a GENA renew's UNBOUNDED
  `connect()` 24 s in lwIP's SYN retries — so consecutive sightings landed >20 s apart and the
  detector reset every pass. That two minutes of frozen Now Playing is what the user reported as
  "hung on the album art". Fixed twice: the pairing window is 120 s (a non-zero RSSI already resets
  the episode, so the window was never the transient-0 guard), and GENA's connect is bounded at 4 s.
- **The diary now carries the last art fetch**: `art=38716B/41ms cdn 12s ago`, so "did a cover
  come down just before the death, how big, from where" survives the reboot. The mirror cannot
  show it — the link dies and the TCP stream just stops.
- **Gate behaviour so far:** 14 acquires, 0 waits, 0 timeouts, max hold 700 ms (`art`) across a
  266 s run of track changes. No overlap has yet needed serialising in the runs observed; the
  waits counter is what will show it earning its keep during a browse-then-play sequence.
- **Still dying.** Deaths observed on the -70/-71 builds: during a Radio browse with tiles (before
  Play All), and twice within a few tens of seconds of a Now Playing fetch. The count is not yet
  a measurement — the reproduction has not been run five times on any one build.

## Second pass (2026-09-07, later) — the gate works, detection was still a full pass

- **Gate, observed:** `[gate] tile waited 279 ms for spotify` / `spotify waited 494 ms for tile`
  during a New Releases → album → artist browse. Overlaps are being serialised.
- **Death #N, on v0.4.2-73:** Search → ABBA → Artists → ABBA Radio → Play All. Diary:
  `netlink:fast dead=57s gapmax=36s@gena now=linkstats art=11830B/0ms cdn 64s ago`. So the cover
  came down (11.8 KB, CDN) about 7 s before RSSI first read 0. The speaker was left in
  Transitioning on the new track and then Stopped — the user saw "nothing played".
- **Why 57 s with a 120 s window:** because a netTask pass over a dead link IS ~50 s. On this board
  `WiFi.status()`/`RSSI()` are RPCs to the C6 that time out at 5 s when the link is gone, and
  `genaTick()` calls one before its (now bounded) connects. The two-sightings design needed the
  NEXT pass. `deadLinkFast()` now probes on the first 0, confirms with one more RSSI read, and is
  independent of pass length (<10 s).
- **Correlation so far:** the last three deaths each followed a Now Playing cover fetch by
  7-20 s — sizes 38 KB, 11.8 KB, 11.8 KB — so the size is not what matters at play time. What else
  play time does: SOAP SetAVTransportURI+Play, a burst of GENA NOTIFYs, an immediate poll, and the
  CDN fetch opening a SECOND idle TLS socket (the SMAPI resolve leaves its session open for 30 s,
  the art client keeps its i.scdn.co connection for reuse). Untested lead: drop both sessions after
  a Now Playing fetch and see whether the play-time death moves.

## Bisection 1 — tiles OFF (v0.4.2-75, 2026-09-07 21:xx): STILL DIES. Tiles are cleared.

Radio → Spotify → New Releases → album → play; then Search → artist radio → Play All (that one
played). Diary: `netlink:fast dead=7s gapmax=6s@gena art=11830B/11ms cdn 21s ago` — the cover came
down 14 s before the link died, with zero tile traffic in the build. Detection: 7 s, as designed.

Tally of the last six deaths: five landed 7-40 s after a Now Playing cover fetch (sizes 38, 11.8,
11.8, 31.9, 11.8 KB — size is irrelevant); one landed at `coord-refresh` 81 min after any fetch.
Play time is the profile. What play time does on the wire, inbound: the SOAP replies to
SetAVTransportURI/Play (small), an immediate poll, the cover fetch (small now), and a BURST OF GENA
NOTIFYs — several KB each of LastChange DIDL, unsolicited, arriving into a 4 KB-stack listener on
core 1 that parses each body as it reads. That is the closest match in this firmware to #184's
mechanism (inbound the host drains slowly → the C6's RX buffers fill → the SDIO link freezes).

**Bisection 2 (next flash): GENA OFF, tiles still off** — one variable at a time. Poll returns to
1 Hz. If the play-time death disappears, eventing is the trigger and the fix is on the listener's
read path (drain the socket into a buffer FIRST, parse afterwards; or ack-and-defer). If it still
dies, what is left is the poll + the cover fetch, and the next bisection is the cover.

Separate bug seen on the way: "the radio station from the Radio link never played" while the same
kind of item from Search did. Not investigated; the two pages must build the Item differently.
