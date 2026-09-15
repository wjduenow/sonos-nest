# 13 — The jukebox link death: shrink the inbound bursts

Status (2026-09-15): **ROOT-CAUSED AND FIXED.** The link death is an ESP-Hosted host-driver bug:
the SDIO streaming RX buffer could not grow from the P4's small DMA-capable internal pool, the
driver dropped the read, and esp_hosted 2.12.13 never delivered C6-to-host data again. Caught on
the USB serial console with a diagnostic build (four deaths, one mechanism). **Fixed** by serving
ESP-Hosted's buffers from DMA-capable PSRAM (`CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y`) with the
64 B L2 cache line esp-hosted-mcu #219 requires. GENA eventing and tile artwork are back on.
Everything between here and that section is the investigation record. Its main conclusions were
wrong: that inbound bursts, eventing or tiles were the trigger, that an esp_hosted version or the
C6 firmware was the fix, and that heap exhaustion was the mechanism. Start with **ROOT CAUSE AND
FIX 2026-09-15** at the end. Tracked in [issue #26](https://github.com/wjduenow/sonos-nest/issues/26).

## What is known (all measured on hardware, 2026-09-07)

- The ESP-Hosted SDIO link between the P4 and the C6 dies under inbound bursts. Four deaths in
  four runs of the same sequence; the last at **Play All**, with the browse and its tiles already
  finished. The diary read `netlink:fast dead=7s gapmax=5s@linkstats` each time.
- Upstream esp-hosted-mcu **#184** names the mechanism: inbound flow control — once the C6's Wi-Fi
  RX buffers fill with inbound TCP, the SDIO host mishandles the backpressure and the link freezes.
  #167 / #121 are the same fault. All open, no maintainer response, no fix.
- **Every transport-side lead is closed.** SDIO is already 1-bit at 10 MHz (#167 says 20 MHz did
  not help). Packet mode instead of stream mode is a hard assert at init (boot loop, USB recovery).
  The C6 reports slave **2.12.11** (`jukebox-c6` probe). *(This said "matching the host library
  exactly" — wrong: the host compiles 2.12.13. See the 2026-09-14 correction. The C6 was then
  flashed to 2.12.13 the same day; the link still dies.)*
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

## Bisection 2 — GENA OFF, tiles off (v0.4.2-76, 2026-09-07 21:21-21:31): SURVIVED

Ten minutes, eight browses (root, New Releases, two albums incl. 19 tracks, an artist, Charts, a
24-row playlist), six play starts across four tracks, transports changing — **zero deaths**, on a
board that died within a minute of play on every eventing build today (six for six). Gate:
22 acquires, 0 waits. The user's unprompted remark: "seems much more responsive."

**Eventing is the trigger.** It fits #184 exactly: a NOTIFY is unsolicited inbound of several KB
that lands whenever the speaker feels like it — at play start, several in a row — and our listener
reads it line by line and parses as it goes on a 4 KB-stack task at priority 1 on core 1. Inbound
the host drains slowly is precisely the condition under which the C6's RX buffers fill and the
SDIO host driver freezes.

**Bisection 3 (next flash): tiles back ON, GENA still off.** If it survives, the stable build for
this device is "poll at 1 Hz" until the listener is fixed — plans/09's traffic saving is not
worth a reboot per play. The fix, when built, is on the receive side: read the whole NOTIFY into a
PSRAM buffer at socket speed and answer 200 BEFORE parsing anything, so the socket is drained the
moment bytes arrive; parse afterwards on the same task.

## Bisection 3 — tiles ON, GENA off (v0.4.2-77, 21:37-21:41): DIED at 3.5 min

Diary: `netlink:fast dead=7s gapmax=24s@gena art=25089B/5ms cdn 43s ago`. The mirror shows the
last minute as continuous gate traffic — tile / spotify / art taking turns at 0.3-1.2 s waits —
through Genres & Moods → a category → a 24-row playlist, with two covers (42 KB, 25 KB). Internal
heap `min=34KB` at that point, the lowest of the evening.

## Conclusion of the bisection (2026-09-07)

| tiles | GENA | result |
|---|---|---|
| on | on | dies within ~1 min of play — six for six |
| off | on | dies (14 s after a 12 KB cover, at play) |
| on | off | dies at 3.5 min, mid tile+browse+cover traffic |
| **off** | **off** | **survives 10 min of the reproduction** |

**Neither source is THE trigger; any sustained inbound stream over the SDIO link is.** This is
esp-hosted-mcu #184 as described — the fault is in the transport, and the only lever on our side
is inbound VOLUME and RATE. The gate (candidate 4) serialises overlaps but does not reduce bytes,
which is why it was visibly working and did not save the link. Candidate 1 removed 146 KB per
track and did not save it either. Detection is now reliably 7 s (that part is done).

**What ships until upstream fixes the driver:**
1. Eventing OFF on the jukebox (`-DGENA_EVENTS` commented out). The 1 Hz poll is a few KB/s of
   small SOAP replies and the user found it *more* responsive. plans/09's 15x traffic saving was
   for the speaker's benefit, not the panel's, and it is not worth a reboot per play.
2. Tile artwork OFF (`-DEXPERIMENT_NO_TILES`) until **candidate 3** lands: fetch tiles for the
   VISIBLE rows only (24 → ~5 per list), repaint on scroll, and pace at ≥400 ms — the SD cache
   already makes every list after the first free. Then re-run the reproduction with tiles on.
3. Candidate 1 stays (a 12-48 KB cover is still the biggest single transfer per track) and so does
   the gate (an overlap is still worse than no overlap); neither is the fix.
4. Untested but cheap, after 2: drop the two idle TLS sessions a cover fetch leaves open.

Left for another day: the Radio-page artist radio that never plays (Search's does); `heapMin`
34 KB during tiles+browse — where that goes.

## Upstream state of the art (read 2026-09-07 evening; corrects the repo's stale notes)

- **#184 is CLOSED (2026-05-19), not open.** Its reporter's inbound stall vanished when they
  replaced consumer 2.4 GHz access points with UniFi WiFi 7 units; they closed it as "root cause
  was the wireless side". Espressif's parting advice: tune `CONFIG_WIFI_RMT_*` and `CONFIG_LWIP_*`
  per `docs/performance_optimization.md`. So "#184 names the mechanism" was never established.
- **#167 / #121 are the `sdmmc 0x107` / `Unrecoverable host sdio state` family** — command
  timeouts on the bus. Espressif suspects hardware (signal integrity, power). **We never log those
  lines.** Our signature is different: writes to the C6 succeed, nothing comes back (RPC
  `Response not received`, RSSI reads 0), no bus error at all.
- **#220 (2026-07-28) is our signature**: "under sustained inbound traffic the SDIO link dies…
  no error, no restart, no further log output… the host keeps running and looks healthy". Espressif
  root-caused it as a dropped RX read that deadlocked the RX path (plus an all-ones bus read that
  was never declared a failure) and fixed it in **esp_hosted 2.12.12** (commit `0985253`,
  2026-07-31, "fix(sdio): recover a dropped RX read instead of deadlocking"; also in 2.12.13 and
  3.0.7). The reporter's overnight measurement: **43 wedges in 21 h on 2.12.11 → 0 in 14.5 h
  patched**, 196 GB inbound.
- ⚠️ *SUPERSEDED 2026-09-14 — the host was 2.12.13, with the fix; see the correction at the end.*
  **We are on 2.12.11 on both sides** — the last version WITHOUT that fix. pioarduino's newest
  platform (55.03.311, 2026-07-24) shipped a week before the fix landed; there is no newer
  pioarduino release yet. The host driver is inside the prebuilt `framework-arduinoespressif32-libs`,
  so it cannot be swapped without rebuilding the Arduino libs, and the slave (C6) firmware needs the
  matching version too (`jukebox-c6` probe env flashes it).
- Also relevant: **#221** (slave Wi-Fi task wedges with `portMAX_DELAY` under overload; measured
  community fixes, open), **#197** (RX mempool exhaustion under sustained load; 2.12.7 made it a
  watchdog reboot rather than a hard wedge), **#240** (a wedged C6 is not recovered by CHIP_PU reset
  on 3.0.6, only by power cycle — matches "power-cycle after every upload"), **#210** (closed: a
  double-free in streaming-mode RX, fixed in 2.12.11 — we have that one).

**Consequence for this plan:** everything above on our side is mitigation of a driver bug that is
already fixed upstream. The real fix is esp_hosted ≥ 2.12.12 on host AND slave, which for this
build means waiting for (or building) a pioarduino/Arduino core that bundles it. Until then: GENA
off, tiles off, detector at 7 s. **Tracked as
[issue #26](https://github.com/wjduenow/sonos-nest/issues/26)** (upgrade checklist inside), with a
weekly cloud routine (Mondays 09:00 PT) that watches pioarduino releases newer than 55.03.311,
reads the bundled esp_hosted version, and comments on #26 when there is news. The access point is
a fairly new unit, so #184's AP explanation does not apply here.

> ⚠️ **"Waiting for a core that bundles it" is no longer sufficient — read the 2026-09-13 section
> at the end of this file before acting on the paragraph above.** It depended on arduino-esp32's
> `^2.9.2` caret picking the fix up automatically, and an approved PR removes that caret.

## Upstream re-read 2026-09-13 — the wait is no longer just a wait

> ⚠️ **Read *Correction 2026-09-14* below first.** This section assumed the jukebox compiles the
> platform package's prebuilt esp_hosted 2.12.11. It does not — so its account of the upstream
> packaging chain is accurate, but its conclusion (that the chain is what stands between us and a
> fix) is not.

Nothing has moved on the packaging side, and one thing has moved the wrong way. The section above
frames the remaining work as waiting for a core that bundles ≥ 2.12.12. That framing depended on
something it never stated, and that something is now up for removal.

**The packaging chain, re-read.** pioarduino `platform-espressif32` is still **55.03.311**
(2026-07-24); the only commit since is `dd01e49` (flash_extra_images paths, 2026-08-02), and there
is no new release. Its libs come from the `espressif/arduino-esp32` release tarball
`esp32-core-3.3.11-libs.tar.xz`, and arduino-esp32 is still **3.3.11** (2026-07-22) — nine days
before the fix landed — with `master` active through 2026-09-01 but unreleased. Meanwhile the fix
has been in the registry for six weeks: 2.12.12 (07-31), 2.12.13 (2026-09-04), 3.0.7 (2026-09-05).

**The unstated assumption: the caret.** arduino-esp32 pins `espressif/esp_hosted: "^2.9.2"`, so the
next libs build picks up 2.12.13 *automatically* — no upstream PR needed, just a release. That is
the only reason "wait for a release" was ever a sufficient plan.

**[arduino-esp32#12879](https://github.com/espressif/arduino-esp32/pull/12879) removes the caret.**
"fix(hosted): Pin version for lower hosted RAM usage" changes it to a hard **`2.12.3`**. Opened
2026-09-02 by me-no-dev, **approved by lucasssvaz on 2026-09-09**, mergeable, checks green. If it
merges before a release, the next arduino-esp32 ships esp_hosted 2.12.3 — eight patch versions
below what this panel runs today, nine below the deadlock fix. **Bumping the platform would then be
a driver downgrade**, and would also give back #210 (the streaming-mode RX double-free) which we
currently have.

The motivation is legitimate and lands in our scarce resource:
[esp-hosted-mcu#191](https://github.com/espressif/esp-hosted-mcu/issues/191), "2.12.6 takes more
memory than 2.12.3, cant start" — `mempool create failed: no mem`, assert at `sdio_drv.c:249`
creating the SDIO DMA buffer pool. DMA-capable **internal SRAM**, which PSRAM cannot back. The
reporter has 32 MB PSRAM, so it is a P4-class board like ours. Still open, no fix in esp_hosted
`main` as of 2026-09-10.

We are on the good side of it — this board runs ~98 KB free internal heap and has never hit the
assert. *(Corrected 2026-09-14: this said "runs 2.12.11". It runs **2.12.13**, i.e. the
pre-allocating mempool #191 complains about, which makes the point stronger.)* Plausibly because `MBEDTLS_EXTERNAL_MEM_ALLOC=y` already moved the 16 KB in +
16 KB out TLS buffers out of exactly that pool.

So the two upstream needs collide: RAM says go back to 2.12.3, our wedging says go forward to
2.12.12. The case was made on #12879 (2026-09-13) — pin `>=2.12.12,<2.13` instead, or fix #191
forward ([esp-hosted-mcu#231](https://github.com/espressif/esp-hosted-mcu/issues/231) caps
`ESP_TRANSPORT_SDIO_MAX_BUF_SIZE` at the IDF DMA limit and looks like it bears on the same
mempool), or make the pool sizing a Kconfig choice — with this device's bisection table as a second
field report, and an offer to test a candidate pin on P4 + C6 hardware.

> ⚠️ **WRONG FOR THE JUKEBOX — corrected 2026-09-14.** Neither the header nor `dependencies.lock` in
> the platform package describes what this env links, because `custom_sdkconfig` rebuilds the libs
> from `managed_components/`. The point about the header drifting still stands; the remedy does not.
> See the correction below for how to read the real version.
>
> ~~`esp_hosted_host_fw_ver.h` IS NOT AUTHORITATIVE — read `dependencies.lock` instead.~~ Those
> macros were hand-maintained and are known to have drifted: esp-hosted commit
> [`627228c`](https://github.com/espressif/esp-hosted-mcu/commit/627228c603445481ae3b1b7a5903631d10eafb46)
> (2026-09-07, "host: report the real component version in `ESP_HOSTED_VERSION_*`") replaced
> hardcoded constants that read `2.12.6` while the component was well past it. The component
> manager's lock file is generated, so it cannot drift:
> ```bash
> grep -A12 'espressif/esp_hosted:' \
>   ~/.platformio-p4/packages/framework-arduinoespressif32-libs/esp32p4/dependencies.lock
> #   version: 2.12.11
> ```
> Both agree in our tree today, but only by luck. #26's step 1 has been corrected to match.

**What the weekly watch tracked (superseded 2026-09-14 — see below):** (1) a pioarduino release newer than
55.03.311, read via `dependencies.lock`; (2) the merge state of #12879 and the value of
`espressif/esp_hosted` in arduino-esp32's `idf_component.yml` on `master`. If it lands at `2.12.3`,
step 1 of #26 needs replacing — the realistic options then are building the Arduino libs ourselves
against a pinned 2.12.13, or staying on 55.03.311 indefinitely with GENA and tiles off.

## Correction 2026-09-14 — the host has had the #220 fix all along

**The jukebox host has compiled esp_hosted 2.12.13 — with the #220 fix — since at least
v0.4.2-66, and it still dies.** Every bisection build (-75, -76, -77) had it. So the fix did not
cure this failure, #220 is not (or not only) our bug, and #26's "upgrade once pioarduino ships
≥ 2.12.12" was waiting for something we already had.

**Why everyone read 2.12.11.** `[jukebox_base]` sets `custom_sdkconfig`, and that makes pioarduino
rebuild the Arduino libs ("HybridCompile"): the IDF component manager runs at build time against
arduino-esp32's `idf_component.yml` and fetches into the project's gitignored
**`managed_components/`**. arduino-esp32 3.3.11 pins `espressif/esp_hosted: "^2.9.2"`, which on
2026-09-07 resolved to **2.12.13** (`managed_components/espressif__esp_hosted/`, fetched 13:17,
its own header reads 2.12.13). Meanwhile `~/.platformio-p4/packages/framework-arduinoespressif32-libs`
keeps the prebuilt 2.12.11 library, header and `dependencies.lock` — accurate descriptions of a
library this env never links. The "authoritative lock file" advice in the section above was
therefore wrong here.

**How it was proven — do this, not a grep of the package.** esp-hosted commit
[`0985253`](https://github.com/espressif/esp-hosted-mcu/commit/098525357e19c81099f2c3769938bd877190a8f5)
(the #220 fix, version bump 2.12.11 → 2.12.12) adds
`PKT_LEN reg reads 0x%08lx (all 32 bits set): SDIO bus fault` to `sdio_drv.c`. That string is absent
at the commit's parent (`cd0e5c3`, `version: "2.12.11"`) and absent from the prebuilt 2.12.11 `.a`,
and present at the fix commit:

```bash
for f in ~/sonos-nest-elf/*/sonos-jukebox.elf; do
  echo "$(basename $(dirname $f)) $(strings -n 8 $f | grep -c 'all 32 bits set')"
done   # every archived jukebox ELF, v0.4.2-66 … -78: 1
grep -E 'VERSION_(MAJOR|MINOR|PATCH)_1' \
  managed_components/espressif__esp_hosted/host/esp_hosted_host_fw_ver.h   # 2 / 12 / 13
```

A present string cannot come from absent code, so this is a lower bound (≥ 2.12.12) that the
compiled copy's header then pins to 2.12.13. (The fix also adds `...failing fast` to
`rpc_core.c`, which does *not* appear in the ELFs — an absent string can be stripped or
gc-sectioned, so it proves nothing either way.)

**The #220 fix is host-only.** Under `slave/`, `0985253` touches only
`esp_hosted_coprocessor_fw_ver.h` (+1/-1, the version number). So the C6 being on 2.12.11 did not
leave the fix half-applied.

**What that leaves.**
- **The C6 slave reports 2.12.11** (`jukebox-c6` probe; that is also a hand-maintained constant, so
  treat it as approximate). Host 2.12.13 + slave 2.12.11 is a **mismatch**, which the host warns can
  cause RPC timeouts. Between `cd0e5c3` and `v2.12.13` there are 44 commits, including slave changes
  to `esp_hosted_coprocessor.c` (+51/-40) and `sdio_slave_api.c`. **Matching the slave to 2.12.13 is
  the next experiment** — plans/07's C6-flashing notes apply.
- Upstream issues that fit a C6-side fault: **#221** (the slave's Wi-Fi task wedges on
  `portMAX_DELAY` under overload) and **#240** (a wedged C6 is not recovered by CHIP_PU, only by a
  power cycle).
- **A cheap discriminator:** the fixed host now logs `SDIO bus fault` at ERROR when it hits the #220
  condition. `ESP_LOGE` goes to the UART, not the TCP mirror (which dies with the link anyway), so
  one serial capture across a death shows whether the host sees a bus fault or the C6 simply goes
  quiet.

**Two build hazards this exposed.**
1. **The driver version floats.** Nothing in the repo records it: a fresh worktree or CI job
   resolves `^2.9.2` on the day it builds. Pinning esp_hosted in the project would make the firmware
   reproducible and let the slave be matched to a known host.
2. **arduino-esp32#12879 would downgrade us through a platform bump.** It hard-pins `2.12.3`, and
   the maintainer's answer (2026-09-14) is that it stays until esp-hosted stops "wasting 80KB of
   RAM". Any pioarduino release built from a core with that pin would re-resolve our rebuild to
   2.12.3. Stay on 55.03.311, or pin the component ourselves before bumping.

**Upstream record.** The 2026-09-13 comment on #12879 argued from this board's data that 2.12.12
fixes the deaths. A correction withdrawing that went up the same day it was found
([comment](https://github.com/espressif/arduino-esp32/pull/12879#issuecomment-5669988494)); the
pin is not ours to contest on this evidence.

## Matched C6 + GENA re-test 2026-09-14 — the version lead is closed; GENA dies on its own

> ⚠️ **Superseded 2026-09-15 by ROOT CAUSE AND FIX below.** The measurements here stand, but GENA
> was never the cause. Its deaths were the SDIO RX allocation bug, and the heap readings missed it
> because they never measured the DMA-capable pool. The next-steps list at the end is obsolete.

**Result: matching the C6 to the host did not cure the link death, and every GENA-on configuration
still dies.** The jukebox went back to tiles OFF + GENA OFF at 21:08 (v0.4.2-98-g1a121d3).

### How the C6 got to 2.12.13 (repeatable, no cable)

1. **Build the slave.** Arduino publishes slave images only up to 2.12.11 (2.12.12, 2.12.13 and
   3.0.7 all 404 at `espressif.github.io/arduino-esp32/hosted/`), so it was built from the same
   source the host compiles, with Arduino's own recipe (esp32-arduino-lib-builder
   `tools/build-hosted.sh`: stock defaults, `set-target` + `build`):
   ```bash
   cp -a managed_components/espressif__esp_hosted/. /tmp/eh && cd /tmp/eh/slave
   docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp -v "$PWD":/project -w /project \
     espressif/idf:v5.5.5 bash -c 'idf.py set-target esp32c6 && idf.py build'
   ```
   The result is `network_adapter.bin`: App version 2.12.13, ESP-IDF v5.5.5, SDIO streaming mode,
   1,226,976 B, within 176 B of Arduino's published 2.12.11 image (1,226,800 B). Its sha256 is
   `e74eb39339f0d3092bd800f6ebf24338327cce04f337c0c6a03787afec9f4662`. Archived with its ELF and
   `sdkconfig.h` at `~/sonos-nest-elf/c6-slave/2.12.13/`.
2. **Serve it** from the build host: `python3 -m http.server 8765` in that directory. WSL inbound
   must be `Allow` (the `/ota` skill's step 0).
3. **Build the probe** with the image baked in, then OTA it over the running app (it uses the app's
   hostname):
   ```bash
   PLATFORMIO_BUILD_FLAGS="-DC6_PHASE=3 -DC6_IMAGE_SHA256=\\\"<sha>\\\" -DC6_IMAGE_URL=\\\"http://<host>:8765/esp32c6-v2.12.13.bin\\\" -DC6_EXPECT_SLAVE=\\\"2.12.13\\\"" \
     tools/pio run -e jukebox-c6
   ```
   The probe downloads into PSRAM and checks the SHA-256 before touching the C6, then writes,
   activates and restarts. `nc <ip> 2323` shows its transcript, and it answers `/ota` so the app
   can go back on. It fetched the image at 14:22:02; after the restart the C6 reported
   **slave 2.12.13**. The first boot's transcript is lost on that restart, because it lives in
   PSRAM. Connect before it finishes, or read the second boot's `RESULT:` line.

The slave has two OTA slots and **no app rollback** (`partitions.esp32c6.csv`, no
`APP_ROLLBACK` in its sdkconfig). 2.12.11 is still in the inactive slot. An image that boots but
never answers SDIO would need the C6's UART.

⚠️ The probe's "host" number and `hostedHasUpdate()` are **wrong for this env**: they compile
against the package's stale 2.12.11 headers. Take the host version from `managed_components/`
or from the guard in `tools/p4_hosted_patch.py`, which now fails any build that resolves
anything but 2.12.13. pioarduino's own `custom_component_remove`/`_add` pin cannot be used for
esp_hosted: removal deletes the component's include directory from the shared package, and the
Arduino core needs it.

### The re-test (host AND C6 on 2.12.13)

| build | tiles | GENA | outcome |
|---|---|---|---|
| v0.4.2-99-g40299cf | on | on | **died at 88 s and 96 s of uptime** |
| v0.4.2-100-g5901135 | off | on | **died at 25 s, 7,503 s and 763 s of uptime** |
| v0.4.2-98-g1a121d3 | off | off | restored 21:08; not yet soaked on the matched pair |

The deaths in detail (all `netlink:fast dead=7s`, `resetReason 3`):

- **88 s** (tiles + GENA): Radio → Spotify → New Releases → album → play (39 KB cover) → artist
  browse (24 rows, tiles; the gate serialised them at 658/430 ms) → `artistTopTracks` browse
  starts → log ends. Heap min 49 KB. Diary `gapmax=2s@coord-refresh … art=39474B/195ms cdn 30s ago`.
- **96 s** (tiles + GENA): the mirror recorder had hung (fixed since), so there is no transcript.
  Diary `gapmax=0s@registrar … art=31921B/90ms cdn 25s ago`.
- **25 s** (GENA only): right after a power cycle. Boot → portal registration → GENA subscribes
  `avt` + `rc` on the coordinator → a 65 KB cover via CDN → dead. No browse, no tiles. Diary
  `gapmax=0s@gena`.
- **7,503 s** (GENA only): ~28 min into use, after ~1.6 h idle. **No burst preceded it**: the last
  cover was 218 s earlier and no browse or tile traffic appears. Internal heap had sat at 20–29 KB
  free, largest block 7 KB, **all-time min 404 B**, for at least 25 min. Diary
  `gapmax=12s@roomstatus … cdn 218s ago`.
- **763 s** (GENA only): **no burst and no heap pressure.** Free heap 75–82 KB for the whole
  session, min 57 KB; last cover 284 s earlier. Diary `gapmax=7s@coord-refresh … cdn 284s ago`.

That last one **rules out internal-heap exhaustion as the cause**. The 7,503 s session looked
like it until the next session died with 80 KB free.

After the 763 s death the same GENA build stayed up 9,865 s until it was replaced. It was idle
from 19:02, so that proves nothing.

Transcripts: `~/sonos-nest-elf/v0.4.2-99-g40299cf/test-logs/` (the 88 s death; the second file
also covers v0.4.2-100's first boot) and `~/sonos-nest-elf/v0.4.2-100-g5901135/test-logs/` (the
404 B session and both later deaths).

### What it means

- **The esp_hosted version is not the lever.** Host and slave match at 2.12.13, the newest 2.x on
  either side. No upstream release on the watch list is known to change this.
- **GENA is sufficient to kill the link on the matched pair, even without a burst.** Two of the
  three GENA-only deaths had no inbound transfer for 3–5 minutes before them. What GENA adds
  permanently: subscriptions renewed against the coordinator, and speaker-initiated TCP
  connections into the panel's listener (`:3401`).
- **It is not proven that GENA is necessary.** On 2026-09-07 (old C6) tiles on + GENA off also
  died, at 3.5 min. That configuration has not been re-run on the matched pair, and tiles off +
  GENA off has not yet been soaked on it either. Do not read the table above as "no GENA, no
  death".
- **Separate bug, unexplained: ~65 KB of internal heap never came back** after about 20 minutes of
  use in the 7,503 s session. It went from 91 KB free idle to ~26 KB, flat, with a 404 B minimum.
  Every `heapwatch` tag stayed ≥ 15 KB (`webconfig.json` lowest), so the holder is untagged code
  or lwIP. The death at 763 s shows it is not what kills the link, but 404 B is far past the
  ~15 KB lwIP floor.

### Next steps, most informative first

1. **A serial capture across one GENA-on death.** They come within minutes to two hours, and the
   25 s post-boot death is the fastest to reproduce. The fixed host logs `SDIO bus fault` at ERROR
   on the all-ones register read, and that goes to the UART, not the TCP mirror. Seeing it or not
   separates "the host sees the bus fail" from "the C6 goes silent".
2. **Soak the restored tiles-off + GENA-off build on the matched pair**, then re-run tiles on +
   GENA off, to settle whether GENA is necessary as well as sufficient.
3. **Instrument before fixing the heap hold:** DMA-capable internal free/largest
   (`MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`, the pool the SDIO driver uses), lwIP socket/PCB counts,
   and the current page on the `[health]` line.
4. **C6-side configuration** (Wi-Fi RX buffers, SDIO queue depth; #221's measured fixes are in that
   area), now that building and flashing the slave is a known procedure.

## ROOT CAUSE AND FIX 2026-09-15 — a failed SDIO RX allocation wedges ESP-Hosted; buffers now from PSRAM

**This supersedes every theory above.** The link death is a host-side ESP-Hosted bug. In streaming
mode the SDIO RX buffer has to grow to the size of the pending stream. That allocation asks for
**DMA-capable internal RAM**, which on the P4 is a small region. When it fails, the driver "drops"
the read, and **esp_hosted 2.12.13 never delivers C6-to-host data again**. The fix is two sdkconfig
lines in `[jukebox_base]` that serve those buffers from DMA-capable PSRAM. With it, GENA eventing
and tile artwork are back on, and the reproduction runs clean. Shipped in v0.4.2-108 (`698b18a`)
and everything after.

### How it was caught: the serial console

- **Where it is:** the CrowPanel's second USB-C port (the CH340K, `1a86:7522`, `/dev/ttyUSB0`).
  The rear port is power only.
- **Opening the port resets the P4** through the auto-reset circuit, even with DTR/RTS set low
  before `open()`. So the recorder opens it **once** and holds it across OTA reboots.
  `~/sonos-nest-elf/serial-captures/2026-09-15-jukebox-serial.log` is the capture (09:00 → ongoing).
- **ERROR-only builds could not have shown this.** `CONFIG_LOG_MAXIMUM_LEVEL=1` compiles out every
  ESP-Hosted WARN/INFO line, including the one that names the failure. The first serial-captured
  death (v0.4.2-100) showed only `rpc_core: Response not received`. It took a diagnostic build:
  - `CONFIG_LOG_MAXIMUM_EQUALS_DEFAULT=n` and `CONFIG_LOG_MAXIMUM_LEVEL_INFO=y`, with the default
    still at ERROR;
  - `CONFIG_ESP_HOSTED_PKT_STATS=y` with `_INTERVAL_SEC=2`;
  - `-DHOSTED_DIAG`, which raises only `stats` and `H_SDIO_DRV` to INFO and the RPC/transport tags
    to WARN (`boards/crowpanel_p4_7in/board.cpp`). `rpc_core` logs every request at INFO, and the
    RSSI read alone would bury the stats.
- **Reading the stats line.** `stats: STA: s2h{in[N] out[N]} h2s{... out(ok[N] drop[N])}
  flwctl{on off}` counts C6→host packets received and delivered, and host→C6 packets sent. The
  second line gives internal free / largest block / min-free and PSRAM. **Its heap numbers are
  `MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL`, not the DMA-capable pool**, and neither is
  `health.heapLargest`. That blind spot is why heap never predicted a death.

### The four captured deaths (v0.4.2-105, GENA on, tiles off, Spotify browsing)

| # | time | `RX buffer alloc failed (len=…)` | internal free / largest block | C6→host frozen at | host→C6 after |
|---|---|---|---|---|---|
| 1 | 09:18:20 | 18,432 B | 34.7 KB / 19,444 B | 2,427 | still counting |
| 2 | 09:24:14 | 15,360 B | 54.1 KB / **31,732 B** | 15,837 | still counting |
| 3 | 09:27:50 | 21,504 B | 67.1 KB / **31,732 B** | 8,539 | still counting |
| 4 | 09:31:17 | 23,040 B | **83.2 KB / 31,732 B** | 8,094 | still counting |

The same sequence each time:
1. A Spotify browse or cover queues a 15–23 KB stream on the C6.
2. `sdio_rx_get_buffer()` (`sdio_drv.c`, streaming branch) tries to grow its double buffer with
   `hosted_malloc_align(len, 64)`. That is `heap_caps_aligned_alloc(..., MALLOC_CAP_INTERNAL |
   MALLOC_CAP_DMA | MALLOC_CAP_8BIT)` unless `MEMPOOL_PREFER_SPIRAM` is set.
3. It fails and logs `W H_SDIO_DRV: RX buffer alloc failed … dropping read`, returning NULL on the
   stated assumption that "the slave resends / the RPC retries".
4. **It doesn't.** `s2h in` froze at the next stats line and never moved again, while `h2s out ok`
   kept climbing. The RSSI RPC timed out 5–6 s later, and `deadLinkFast()` rebooted at dead=7s.
   A C6 reset always recovers it.

Deaths 2–4 failed with **31.7 KB general internal blocks free**, larger than the request, and death
4 with 83 KB free. So the pool that runs out is the DMA-capable subset, which none of our metrics
measured.

**How this explains everything upstream of it:**
- **Inbound bursts** decide the size of the pending read.
- **GENA, tiles and browsing** add inbound traffic and heap churn.
- **"Heap exhaustion" looked plausible**, including the 404 B session on 2026-09-14, but it was
  never the real variable. The DMA-capable pool can be empty while general heap is fine.
- **#220's "dropped RX read deadlocks RX"** is this failure mode. 2.12.12's fix (`0985253`) covers
  the all-ones register read, not the allocation-failure drop.

### The fix (`[jukebox_base]` custom_sdkconfig)

```ini
CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y   ; try DMA-capable PSRAM first (CONFIG_SOC_PSRAM_DMA_CAPABLE=y on the P4)
CONFIG_CACHE_L2_CACHE_LINE_128B=n
CONFIG_CACHE_L2_CACHE_LINE_64B=y           ; REQUIRED with it — esp-hosted-mcu #219
```

- **The cache line has to move with it.** #219 shows `PREFER_SPIRAM` with a 128 B L2 cache line
  leaving half the 1,600 B zero-copy TX pool blocks DMA-misaligned (`Failed to send data: 258 …
  Unrecoverable host sdio state`). The 128 B line was inherited from Arduino's prebuilt P4
  sdkconfig (`esp32p4_es/sdkconfig.orig`). 64 B is IDF's own default for a 256 KB L2 cache.
- **It frees internal RAM besides.** At the first stats line, internal free went from 175 KB to
  264 KB, and the largest block from 74 KB to 139 KB. During use the internal low is **145 KB**,
  against 17–58 KB on the diagnostic build.

### Evidence

| build (all host + C6 2.12.13) | GENA | tiles | buffers | result |
|---|---|---|---|---|
| v0.4.2-105 (diag) | on | off | internal DMA | **4 deaths in ~13 min of use** (table above) |
| v0.4.2-106 (diag + fix) | on | off | PSRAM | 19 min, 11 browses, 53,600 C6→host packets, **0 failures** |
| v0.4.2-107 (diag + fix) | on | **on** | PSRAM | 10+ min, 14 tile fetches, **0 failures** |
| v0.4.2-108 → -112 (shipping) | on | on | PSRAM | clean through 12:25 |

Across all fix builds from 09:33:48 to 12:25:20 (2 h 52 min, 5 boots, every one an OTA): 34 Spotify
browses, 31 covers, 26 tile/gate hand-offs; **0 dead-link restarts, 0 RPC timeouts, 0
`RX buffer alloc failed` (visible on -106/-107), 0 `Failed to send data`**. That is strong against
the fast death. A longer soak should still confirm it.

### What stays true, and what does not

- **Still worth keeping:** the inbound gate, the 300 px CDN cover, the 7 s dead-link detector (it
  is how every capture got a clean reboot), and the esp_hosted version guard.
- **No longer true:**
  - "GENA/tiles off until the link death is cured": both are back on (`-DEXPERIMENT_NO_TILES`
    removed, `-DGENA_EVENTS` on).
  - "The C6 is the open lead": C6-side config is not needed.
  - "Matching versions / upgrading esp_hosted is the fix": the fix is buffer placement. The bug is
    still in 2.12.13 and needs reporting upstream.
- **The upstream bug is still real**, and is filed as
  [esp-hosted-mcu#243](https://github.com/espressif/esp-hosted-mcu/issues/243) (2026-09-15).
  The retry `0985253` added (`pending = true`) never runs: the first pass already cleared
  `NEW_PACKET`, so the retry pass `continue`s past the read and then blocks waiting for an
  interrupt the C6 does not re-raise. That is why each capture shows the warning exactly once. Any
  board running streaming mode with internal DMA buffers can wedge the same way. Moving the buffers makes the allocation not fail; it does not fix the
  dropped-read path. A future esp_hosted bump must keep `PREFER_SPIRAM` + 64 B, or confirm the drop
  path is fixed.

### Found on the way (not the link death)

- **An OTA push can fail with `[ota] error 3` / `Broken pipe`** while the link stays healthy: the
  counters kept moving, and a retry succeeds. That is the ordinary transient CLAUDE.md already
  describes.
- **A 5 s `/api/config` poller** (the external health watcher used on 2026-09-14) is ~2.1 KB per
  call and did not cause deaths. It did make `webconfig.json` the `heapLow` tag. Serial replaces
  it for this kind of test.
- **Explicit Spotify tracks.** An account with explicit content off makes Sonos refuse the track
  silently. Play returns UPnP 701, or is accepted and sits at STOPPED. Now Playing still showed
  the title. The jukebox now reports refusals (`playFailSeq`) and badges explicit rows from
  Spotify's `<explicit>1</explicit>` (plans/12).
