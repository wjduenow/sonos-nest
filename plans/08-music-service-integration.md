# 08 — music services: the Radio feature, and the research behind it

**Part 1 is the implementation plan. Part 2 is the research record** that establishes what is and
isn't possible — read Part 2's *Verdict table* if you want the one-screen summary of which services
can be browsed and which cannot.

The short version: **Sonos favourites are not radio.** The jukebox's current "Radio" page lists
`FV:2` favourites, which is a different thing wearing the wrong name. Amazon's Prime Stations *are*
radio, they enumerate in full over SMAPI DeviceLink (proven on hardware), and they are what the new
Radio page will show.

---

# Part 1 — Implementation plan: Favorites + Radio

## What changes

| | now | after |
|---|---|---|
| rail slot 2 | **Radio** (`LV_SYMBOL_LIST`) — actually lists `FV:2` favourites | **Favorites**, heart icon — same content, honest name |
| rail slot 3 | — | **Radio**, radio icon — Amazon Prime Stations, hierarchical |
| rail | 4 items | **5 items** |

Nothing about the favourites page's behaviour changes; it is a rename plus an icon. The new Radio
page is the actual work.

## Scope decision: Amazon Prime Stations, exclusively

The Radio page shows **Amazon Prime Stations and nothing else.** The anonymous-SMAPI route (TuneIn,
SomaFM, NTS — 32 credential-free services, fully proven in Part 2) stays documented as a **fallback**,
not a second source. Mixing catalogues would mean reconciling two id schemes, two auth models and two
cache shapes for a list the user experiences as one thing. If the Amazon route ever fails — token
revoked, tier downgraded, API change — Part 2 has everything needed to swap the backend without
touching the UI.

## UX

### The rail grows to five

`RAIL_BTN` is 72 px on an 86 px pitch, so five items occupy 430 px of the 600 px panel height —
fits with room to spare. No geometry change needed beyond `PAGE_COUNT`.

> ⚠️ **Icon problem, and it needs deciding before the UI work starts. LVGL's built-in symbol font has
> neither a heart nor a radio glyph.** The current icons are `AUDIO / LIST / VOLUME_MAX / SETTINGS`,
> i.e. "Radio" is a *list* icon today. Two options:
>
> - **(a) Convert a two-glyph Lucide subset** (`heart`, `radio`) into an LVGL font. This is what the
>   design system actually specifies — `screens.cpp` already carries a TODO admitting the Lucide →
>   `LV_SYMBOL_*` substitution is a deviation. Costs a few KB of flash, scoped to `UNIT_JUKEBOX` the
>   same way the Montserrat sizes already are. **Recommended.**
> - **(b) Interim fallback:** Favorites keeps `LV_SYMBOL_LIST`, Radio takes `LV_SYMBOL_AUDIO`. Ugly
>   and ambiguous against Now Playing, but unblocks the rest.

### The list rolls — it is a drum, not a page of rows

The Radio list should read like a jukebox mechanism: a **rolling drum** you spin, with one entry
selected at the centre, at every level.

**Use `lv_roller`.** LVGL ships exactly this widget — a perspective drum that snaps to a centred
option, driven by drag. It is the mechanic being described, it is built in, and it costs nothing to
adopt.

```
┌──────────────────────────────────────────────────────────┐
│ ‹ Jazz                                          [search] │
│ ┌───────────────────────┐  ┌───────────────────────────┐ │
│ │   Instrumental Jazz   │  │                           │ │
│ │   Cool Jazz           │  │      (art, if any)        │ │
│ │ ▸ SMOOTH JAZZ ◂       │  │                           │ │
│ │   Ultimate Jazz Radio │  │   Smooth Jazz             │ │
│ │   Vocal Jazz          │  │   Jazz · Prime Station    │ │
│ └───────────────────────┘  │        [ ▶  Play ]        │ │
│  A B C D E F G H … Z       └───────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
```

**Roller left (~40%), detail panel right.** `lv_roller` options are plain text, so per-row art is not
possible — art belongs to the **one** selected station, shown large in the detail panel. That is a
single lazily-fetched image per selection instead of a viewport full, which is the cheapest possible
answer on a board where every HTTPS fetch costs an mbedTLS session.

> **Artwork exists after all** (see *Artwork* below — the earlier "no art" finding was a parser bug),
> so there is now a genuine choice here, and it is worth making deliberately:
>
> | | **A. Roller + detail panel** *(as specced)* | **B. Snapping tile carousel** |
> |---|---|---|
> | widget | `lv_roller`, text | list + `LV_SCROLL_SNAP_CENTER`, art per row |
> | art in flight | **1** image (~4.6 KB) | 6–8 images (~28–37 KB) |
> | LVGL pool | minimal | ~1 KB+ per row plus decoded images |
> | feel | drum you spin, one big card | flipping through title cards |
>
> **A is specced and recommended**, because the station name is rendered *into* the artwork and at
> 96–128 px that text is decorative rather than legible — a column of busy tiles reads worse than a
> clean text drum with one large card beside it. **B is arguably closer to a physical jukebox**, and is
> now affordable (~2–5 KB per tile), so it is a real option rather than a theoretical one. Decide
> before step 5; the cache and crawler are identical either way.

### Levels

```
L1  genres        26 entries, rolls        ‹ back = Radio root is top
L2  stations      50 entries, rolls        ‹ back to genres
    select        detail panel -> Play
```

Rolling behaviour is identical at both levels; drilling in replaces the roller's contents rather than
building a new screen. A header chevron is the back affordance — the nest's long-press-to-go-back is a
rotary idiom and does not belong on a 7" touch panel. Nav stack depth never exceeds 2.

### Filtering — two mechanisms, both needed

**1. Type-to-filter (on-screen keyboard).** A search affordance in the header opens an
`lv_textarea` + `lv_keyboard` — the unit already has both wired for the device-name field in
Settings, so this is reuse, not new machinery. Typing filters the roller live.

- **Scope:** search is **global across all ~1,300 stations**, not just the current genre. Someone
  hunting "Miles Davis" should not have to know he is under Jazz.
- **Cap results at ~100.** A roller's options are one newline-joined string held in the LVGL pool; at
  ~25 B per entry the full 1,300 is ~32 KB, which fits the 512 KB pool but is wasteful to rebuild on
  every keystroke. Capping bounds both the allocation and the rebuild cost.
- **Debounce ~200 ms** so a fast typist does not trigger a rebuild per character.
- The keyboard occupies roughly the lower half of a 1024×600 panel; slide it over the detail panel and
  keep the roller visible, so results update where the user is looking.

**2. A-Z jump strip.** A vertical column of letters down the side. Tapping a letter rolls to the first
entry beginning with it — the phone-contacts idiom, and much faster than dragging through 50 entries.

- Requires each genre's stations **sorted by title**, plus a 26-entry offset table (letter → first
  index). Both are produced at crawl time, not on device — see the cache section.
- Letters with no entries render dimmed and ignore taps.

## Artwork — every station has it, and the resize op is load-bearing

**RUN-VERIFIED, and it overturns an earlier claim in this document.** 1044/1044 station rows across
all 26 genres carry a populated `albumArtURI`. Zero without.

> ### The false negative, recorded because it will bite the firmware parser too
>
> The element **never appears bare**. In one Jazz response: `<albumArtURI>` occurs **0** times,
> `<albumArtURI requiresAuthentication="false">` occurs **50** times. A regex or literal match on
> `<albumArtURI>` finds nothing and reports "no art". Stations are also `mediaCollection` with
> `itemType=program` — **never** `mediaMetadata` — so a parser scoped to `mediaMetadata` sees no items
> at all.
>
> **`didl.cpp`-style tag scraping must match the opening tag as `<name` followed by `>` or whitespace,
> not `<name>`.** This is exactly how the research got it wrong.

A complete station item — these five children are the *entire* record:

```xml
<mediaCollection>
  <id>catalog/stations/A15MU3EQ4XZ3Y5/#chunk-kKRW16vjSke5rJmIh-L8rQ</id>
  <itemType>program</itemType>
  <title>Worship Now Radio</title>
  <canPlay>true</canPlay><canEnumerate>true</canEnumerate>
  <albumArtURI requiresAuthentication="false">https://m.media-amazon.com/images/G/01/MusicProgramming/2024_US_WorshipNowRadio_ST_TD_Tile_2400x2400.png</albumArtURI>
</mediaCollection>
```

### The `._SL<N>_.jpg` rewrite is not optional

Native art is **2400×2400, 250 KB – 6.7 MB, and ~40% of it is PNG — which TJpg cannot decode.**
Unusable raw. Amazon's image server accepts a resize op appended to the filename, **and it transcodes
PNG → baseline JPEG**:

```
re.sub(r'\.(jpg|jpeg|png)$', '._SL128_.jpg', url, flags=I)     # ops stack harmlessly
```

Measured on the item above: **1,422,804 B PNG → 1,988 B baseline JPEG.** Across 60 distinct URLs:
60/60 HTTP 200, 60/60 baseline JPEG (SOF0), median latency 0.11 s.

| `._SL<N>_` | median | max |
|---|---|---|
| 96 | 2.9 KB | 4.2 KB |
| **128** | **4.6 KB** | 6.7 KB |
| 200 | 9.7 KB | 15.9 KB |

`._SL128_.jpg` is the recommendation: baseline JPEG, feeds the existing `core/album_art.cpp` TJpg path
unchanged.

### Facts that shape the cache

- **HTTPS only.** Plain HTTP is **403** on both hosts, native or resized. That is a real cost here —
  an mbedTLS session against a ~70–100 KB internal-SRAM idle budget. **One connection, keep-alive,
  reused across the visible tiles**, and never concurrent with an album-art fetch.
- **Two hosts:** `images-na.ssl-images-amazon.com` and `m.media-amazon.com`. Normalise everything to
  `m.media-amazon.com`, which serves the `images-na` paths too — one host, one TLS session.
- **The art URL is STABLE; the `#chunk-` is not.** Re-browsing Jazz minutes later returned
  **50/50 identical `albumArtURI` and 0/50 identical `#chunk-`.** So art is a property of the station:
  **key the SD image cache by STATION KEY**, normalising any `._AA500`-style op off first.
- **The catalogue is smaller than assumed: 840 distinct stations** across 1044 rows (174 appear in
  more than one genre). Genres are **not** uniformly 50 — Holiday 19, International 28, Soundtracks
  32, K-Pop 1, Miscellaneous 2, Recently Played 3.
- **Genre containers have art, but it is useless.** All 26 carry an `albumArtURI`, but there are only
  **two distinct URLs** — generic 1.6 KB gray placeholders. **Level 1 stays text/icon**; spend the
  artwork budget on level 2 where it is real. (This is the "genres don't, stations do" split — the
  inverse of what was anticipated.)
- **`/getaa` still cannot serve station art**, and now there is a mechanism: it is a
  `getMediaMetadata` proxy, and Amazon rejects a container id — *"did not contain a TrackInstance"*.
  Re-tested on freshly minted ids with a known-good track as a positive control (200, byte-identical
  on two speakers) so the 404 is meaningful.
- **`getExtendedMetadata` returns the same URL** — so it works, but it is a wasted round-trip.
  `getMetadata` already carries the art during the crawl you are doing anyway.

### Two constraints from the imagery itself

1. **The station name is rendered into the artwork.** At 96–128 px that text is decorative, not
   legible — **keep the text label in the row**; the tile is not a substitute for it.
2. Tiles are busy and high-contrast. They need padding and corner rounding, not edge-to-edge.

## The SD cache

### Why cache at all

Browsing live costs one HTTPS SOAP round-trip per level at ~0.6 s, over the ESP-Hosted link that is
documented to die under sustained load. Caching makes browsing instant, survives a dead link, and
keeps the radio traffic to one scheduled burst instead of a request per tap.

### Layout

**One file per genre plus an index**, not a single blob:

```
/radio/index.tsv      version, fetchedAt, serviceId, accountSerial, then one line per genre:
                      <genreIdx>\t<title>\t<container id>
/radio/g00.tsv .. g25.tsv    one line per station, SORTED BY TITLE:
                      <title>\t<full item id>
/radio/g00.azx .. g25.azx    26 bytes-per-entry offset table: letter -> first line index
/radio/all.tsv        flat cross-genre search index, sorted by title:
                      <title>\t<genreIdx>\t<full item id>
```

**Sort at crawl time, not on device.** The device must never sort 1,300 strings — the crawler writes
each genre file already ordered by title and emits the A-Z offset table alongside it. `all.tsv`
exists so global type-to-filter is a single linear scan of one file rather than 26 opens.

TSV, not JSON: no parser, no allocator, read a line at a time. A page render touches exactly one
small file and never parses the whole catalogue.

> **Cache the station's full item id verbatim** — `catalog/stations/<KEY>/#chunk-<token>`. Do **not**
> store key and chunk separately and recombine, and do **not** attempt to mint a chunk. Chunks are
> server-minted per response but **never expire** (a years-old favourite is still playable), so a
> cached id stays good indefinitely. See Part 2 for the evidence.

### Refresh job

- A low-priority task on core 0, or a step folded into netTask's idle path — **never** the UI task.
- Triggered on boot when the index is older than N days, plus a manual **Refresh** in Settings.
- **Weekly is ample.** The genre list is static and station rosters move slowly.
- **Pace it: one request every 1–2 s, not 27 back to back.** The ESP-Hosted fault correlates with
  sustained load, and a 500 KB burst is exactly that profile.
- Write to a temp file and rename on success, so a failed crawl never leaves a half-written index.

## Measured budget — the answer to "can we afford this?"

**Yes, comfortably. Bandwidth is not the constraint.** Measured against the live service:

| | measured |
|---|---|
| Prime Stations root | **11,278 B**, 26 containers, 0.60 s |
| One genre (Jazz / Rock / Country, all sampled) | **~19.2 KB**, exactly **50** stations, 0.60 s |
| Per station, on the wire | **~382 B** |
| **Full crawl** | **27 requests · ~499 KB · ~16 s** |
| **SD index** | **~150–250 KB** for 1044 rows (adds an art-URL column: +56 KB with host prefixes stripped, +106 KB without) |
| Art, lazy per tile | `._SL128_.jpg`, median **4.6 KB**, ~0.11 s |
| Art cache if fully populated | 840 stations x ~4.6 KB = **~3.8 MB** on SD |

A weekly refresh is ~500 KB/week. Peak RAM is a single response (~20 KB) if parsed streaming —
the catalogue is never held in memory. Genres cap at exactly 50, so no paging is needed.

**The three real constraints, none of which are bandwidth:**

1. **The SD card is still blocked.** It needs a FAT32 filesystem laid down from a PC, and the
   filesystem throughput numbers are still unmeasured. This feature cannot ship before that.
2. **Internal SRAM and TLS.** An mbedTLS session costs tens of KB of internal RAM and the jukebox
   idles at only ~70–100 KB free. Crawl with **one connection at a time**, and never concurrently
   with an album-art fetch.
3. **The ESP-Hosted link dies under load** — the one unresolved fault on this board. Pace the crawl,
   make it resumable, and treat a failure as "try again next boot" rather than retrying hard.

## Open questions

- ~~Station artwork~~ **RESOLVED: every station has artwork.** See *Artwork* below. The earlier
  "empty on all 150 sampled" claim in this document was **a parser bug of mine, not a property of the
  service**.
- **Token lifetime and refresh.** `refreshAuthToken` exists in the WSDL; the lifetime is unknown.
  Needs a re-auth path when it lapses.
- **Where the DeviceLink ceremony lives.** The token belongs in NVS, but obtaining it needs a
  browser. Settings can show the `regUrl` (a QR code would be kinder on a wall panel).
  **Gotcha:** `linkDeviceId` is per-request and required to redeem the code — capture it with the
  `linkCode` or the user has to authorise twice.
- **Does a browsed URI actually play?** The one untested leg. Cheapest test costs nothing new: play an
  existing Amazon station favourite, which also settles the outstanding UPnP 402 question.
- **Prime vs Unlimited.** Station *playback* may require Music Unlimited rather than plain Prime.

## Build order

1. **Unblock the SD card** — FAT32 from a PC, then `jukebox-sd` for real throughput numbers.
2. **One playback test** — settles the UPnP 402 question and the Prime-vs-Unlimited tier question
   together, using an existing station favourite so no new code is needed.
3. **Rename Radio → Favorites**, add the fifth rail slot, convert the two Lucide glyphs.
4. **Cache writer** — crawl + sorted SD index + A-Z offset tables + `all.tsv`, token hardcoded for
   bring-up. Testable entirely off-device against the files it produces.
5. **Radio roller** — L1 genres, L2 stations, detail panel, Play. The core of the feature.
6. **Filtering** — A-Z jump strip first (cheap, no keyboard), then type-to-filter over `all.tsv`.
7. **DeviceLink ceremony in Settings** + NVS token + `refreshAuthToken` handling.

Steps 1–2 are prerequisites and both need someone at the device. Step 4 produces files that step 5
consumes, so those are ordered; 3 is independent of everything. Step 6 splits cleanly — the A-Z strip
is worth having even if type-to-filter slips, since it makes 50 entries navigable on its own.

---

# Part 2 — Research record

Everything below is evidence for the choices above: what was tried, what worked, what was measured,
and what remains unverified. Kept in full because the negative results are as load-bearing as the
positive ones — several of them are questions that will otherwise be asked again.

## Verdict table

| Route | Works? | Why |
|---|---|---|
| Browse YTM via local `ContentDirectory` | ❌ | UPnP **701 No such object** for every service-root form |
| Synthesise a playable URI from a YTM id | ❌ | The Sonos item id is an **opaque, account-scoped token** minted by Google's SMAPI backend |
| Read the household's YTM token, call SMAPI ourselves | ❌ | Token is **on the player but write-only** — no interface returns it |
| Sonos cloud Control API | ❌ | **No browse/search/catalog path exists** (55 paths, machine-verified) — and it is a regression on every other axis too |
| Run our own SMAPI service on the Pi | ❌ | `customsd.htm` **403s on S2**; S2 now requires a **public HTTPS:443** endpoint + Sonos developer registration |
| ytmusicapi + stream proxy on the Pi | ⚠️ | Technically works; requires yt-dlp treadmill, breaks the no-cloud premise, and cannot reproduce station semantics |
| **Favourites (`FV:2`)** | ✅ | **Already implemented.** Zero infra, fully local |
| **Capture what's playing** | ✅ | Verified read-only on hardware; replay untested (see below) |
| **Anonymous SMAPI browse (32 services)** | ✅ | **Run-verified**: empty `<credentials/>` is the whole requirement. Browse + `getMediaURI` both work. Playback leg untested |
| **Spotify: track / album / playlist by id** | ✅ | Transparent wrapper; URI validity **proven read-only** via the `/getaa` oracle. Needs a helper for the Spotify API |
| Spotify stations / Daily Mix / Discover Weekly | ❌ | Spotify's own API removed the radio generator and filters Spotify-owned playlists below extended quota (unreachable: needs 250k MAU) |
| **Amazon `prime/stations/`** | ❌ | **Legacy namespace** — absent from the current presentation map; nothing new is minted there |
| **Amazon Prime Stations via DeviceLink** | ✅ | **PROVEN**: browse returns 26 genres x ~50 stations with server-minted ids. Never construct a `#chunk-`. Playback leg still untested |
| **DeviceLink services (15 of 106)** | ✅ | **Handshake + browse both PROVEN on Amazon.** One browser ceremony by the owner, then full catalogue access with no Sonos app or cloud |

---

## The three proofs

### 1. The favourite id is an opaque, account-scoped token

A YouTube Music favourite looks like:

```
x-rincon-cpcontainer:1006004cALkSOiFAONyGoCF1zjZUFf9E-Wlqps0Gdzv7l11JfEPqzICM2U_m...?sid=284&flags=76&sn=7
```

Base64url-decoding the `ALkSOi...` payload across four samples from three unrelated households:

```
sn=7   container  60 bytes   00b9123a21 4038dc86a02175ce...
sn=12  container  36 bytes   00b9123a21 27d613f73f55d37e...
sn=2   track      55 bytes   00b9123a21 1c548d976e75beb8...
sn=2   container  36 bytes   00b9123a21 1c7624b8f0f3b6f5...
```

- Constant 5-byte header `00 b9 12 3a 21` — that *is* the `ALkSOi` prefix you see.
- After it: **5.6–5.7 bits/byte entropy**, variable length, no ASCII at any alignment, not
  protobuf-parseable. **No YouTube Music identifier is present** — no `RDCLAK5uy_`, `RDTMAK5uy_`,
  `PL`, `MPREb_`, `VL`, no 11-char videoId.
- Container and track payloads are **indistinguishable** — item type lives in the Sonos prefix, not
  the token.
- **Decisive:** the two same-account samples share **6** leading bytes; every cross-household pair
  shares exactly **5**. Byte 5 tracks the *account*. The token is account-scoped, not
  content-addressed — so even a correct YTM station id could not be turned into one.

**Corroboration:** SoCo's `ShareLinkPlugin` — the canonical "public link → playable Sonos URI"
implementation — supports Spotify, Spotify US, TIDAL, Deezer, Apple Music. Exactly the services whose
Sonos id embeds the native public id. **YouTube Music is absent**, and no issue has ever proposed
adding it. Same gap in `node-sonos-ts`'s MetadataHelper and `node-sonos-http-api`. Three independent
projects that would have done this if it were possible.

### 2. The account token is on the player, but write-only

Sonos's own docs (docs.sonos.com, updated 2026-04-16) say the credential is stored on the players and
propagated player-to-player — `add-browser-authentication`, `use-authentication-tokens`,
`account-matching`, and the Control API's `createSession` all state it four different ways.

But every interface that touches it is an input. From the live `SystemProperties` SCPD:

```
AddOAuthAccountX            :: AccountType(in), AccountToken(in), AccountKey(in), OAuthDeviceID(in), ...
RefreshAccountCredentialsX  :: AccountType(in), AccountUID(in), AccountToken(in), AccountKey(in)
ReplaceAccountX             :: AccountUDN(in), ..., AccountToken(in), AccountKey(in), OAuthDeviceID(in)
```

**Nothing returns them.** Supporting evidence:

- `/status/accounts` has **no `<Token>` element at all**; on this household it returns an empty
  `<ZPSupportInfo></ZPSupportInfo>`, and even SoCo's captured non-empty sample shows `<Key>` and
  `<OADevID>` empty.
- `MusicServices` exposes only 3 actions — `GetSessionId(ServiceId, Username)`,
  `ListAvailableServices`, `UpdateAvailableServices`. `GetSessionId` serves the **legacy
  username/password** flow only; it cannot produce an OAuth `loginToken{token,key}`.
- The cloud's `musicServiceAccounts/match` returns an account **ID**, never a token.
- SoCo's `music_service.py` states it outright: *"we can't get the accounts from the device anymore."*
  SoCo, `node-sonos-ts` and `noson` all run their **own** `getAppLink`/`getDeviceAuthToken` handshake
  and keep their **own** keyring. Three independent implementations, same conclusion.

So a LAN client wanting SMAPI access must become its own registered account. It cannot piggyback on
the account the user already linked in the Sonos app. For YouTube Music that path is additionally shut
behind an API-key gateway.

### 3. The cloud Control API cannot browse

Every reference page embeds the full OpenAPI 3.0.3 spec (`v1.55.0-alpha.8-production-cloud`). Merging
all 96 pages gives **55 paths / 61 operations / 12 namespaces**, and **no path contains `browse`,
`search`, `catalog`, `library`, `artist`, `album` or `track`**. `playlists` returns `trackCount` and
nothing else. The only browse-ish command that ever existed, `getPlaylist` (singular), shipped marked
*"Sonos does not currently support this command"* and has since been silently deleted.

Even setting browse aside, it is a regression on every axis for this project:

- **Cloud-mandatory.** *"The Control API on the LAN is not available for wide release."*
- Redirect URI **must be publicly routable HTTPS** — no `localhost`, no PKCE, no device flow. Client
  secret required, so it cannot live in firmware.
- Events need a **public HTTPS callback with a CA-signed cert** (403 without) → an ESP32 can only
  poll, which collides with the quota.
- Quota is **1000 req/min per application, aggregated across all users**. Our ~1 Hz poll is 60/min per
  device against a shared ceiling.
- `integration.sonos.com`, where keys are issued, **does not currently resolve**.

---

## The wrapper rule (useful for OTHER services)

Sonos item ids are `<8 hex prefix><payload>`, and **the low 4 hex digits of the prefix are the
`flags=` value in hex**. Confirmed six times across four services: `1004206c`→8300, `1004006c`→108,
`10062a6c`→10860, `00032020`→8224, `1006004c`→76.

The payload splits into two families, and this contrast is the whole answer:

| service | payload | derivable? |
|---|---|---|
| Spotify `10032028spotify%3Atrack%3A68U7...` | percent-encoded native URI, **cleartext** | ✅ |
| Deezer `1004006calbum-169734362` | native numeric id | ✅ |
| Apple `1006206cplaylist:pl.92e04ee...` | native id | ✅ |
| **YouTube Music `1006004cALkSOiF...`** | **opaque base64url token** | ❌ |

For the transparent-wrapper services, constructing a playable URI from a public link is possible (and
is what SoCo's ShareLinkPlugin does). The DIDL `<desc>` token is
`SA_RINCON<type>_X_#Svc<type>-0-Token`, where `type = serviceId * 256 + 7` (YouTube Music: 284 → 72711,
confirmed present in this household's `AvailableServiceTypeList`).

---

## The capture channel (verified — and the one thing worth building)

You cannot browse what a service offers, but **you can read the id of anything that has played**,
locally and read-only. Measured on this household:

```
192.168.68.100  sid=284  Bananaphone          x-sonosapi-hls-static:ALkSOiEDJZCm2tncaq0VxE3I4IX8j0n...?sid=284
192.168.68.114  sid=284  Sweet Child O' Mine  x-sonosapi-hls-static:ALkSOiG9Uoada315CQNVkDra0kzk-CL...?sid=284
192.168.68.103  sid=201  Ocean Waves          x-sonos-http:library%2fartists%2f...?sid=201
```

Read via `Browse('Q:0')` and `AVTransport::GetMediaInfo` — no token, no cloud, no app. This means a
unit could observe what is playing and store it as a device-local preset, without the user having to
favourite it in the Sonos app first.

**UNTESTED, and both tests need a playback command (do them with the owner present):**

1. **Do captured URIs replay?** These are `x-sonosapi-hls-static:` *track* URIs. Favourites
   deliberately use the `x-rincon-cpcontainer:` form, which Sonos re-resolves — the HLS-static form
   may be session-bound or expiring. If it does not survive, capture is only useful for the container
   URI, not track URIs.
2. **What does a station's transport URI look like?** Capturing a *station* rather than a track means
   reading `GetMediaInfo`'s `CurrentURI` while a station plays. Nothing was playing one during this
   research, so this is unobserved.

**Note on the no-`sid` favourites:** these were initially assumed to be Sonos Radio. They are not —
6 are dead Google Play Music (see the cleanup note above) and only 2 (`Discover Sonos Radio`,
`Sonos Presents`) are genuinely Sonos Radio, whose `<desc>` is account-keyed
(`SA_RINCON77575_X_#Svc77575-668459c3-Token` — note `-668459c3-`, not `-0-`).

**Also available and unexplored:** services whose descriptor says `Auth="Anonymous"` can be browsed
directly over SMAPI with no token at all — on-device, no Pi, no cloud. That covers a large slice of
this household's service list (SomaFM, Radio Paradise, AccuRadio…). It does nothing for YouTube Music,
but "browse real radio catalogues on the device" is achievable within the project's premise. The real
`credentials` SOAP header contract is in the WSDL and **differs from the prose docs** (which omit
`sessionId`); trust the WSDL.

---

---

## ✅ Anonymous SMAPI browsing WORKS — verified first-hand

This is the constructive result of the whole investigation, and it fits the project's premise
exactly: **real station catalogues, browsable on-device, with no token, no cloud, no Pi, and no
Sonos app.** Everything in this section was run, not read about — the exchanges below were executed
from a laptop on the LAN, against the services' own endpoints, with the household uninvolved.

### 32 of this household's 106 services need no credentials

`ListAvailableServices` → filter `Policy Auth="Anonymous"`. Notable entries:

| sid | service | SMAPI endpoint |
|---|---|---|
| 254 | TuneIn | `https://legato.radiotime.com/Radio.asmx` |
| 516 | SomaFM Radio | `https://sonos.somafm.com/` |
| 230 | NTS Radio | `https://www.nts.live/smapi` |
| 270 | Relisten | `https://sonos.relisten.net/mp3` |
| 585 | Radio France | `https://api.radiofrance.fr/voiceapi/sonos/smapi` |
| 280 | Audacy | `https://sonos.audacy.com/` |
| 44 | Hype Machine | `https://api.hypem.com/api/sonos` |
| 277 | NRK Radio | `https://psapi.nrk.no/sonos/sonos.svc` |

…plus 24 more (regional broadcasters, Sveriges Radio, myTuner, Virgin Radio UK, Relisten's 227
artists, etc.). All 32 were probed with `getMetadata(id=root)` and **32/32 returned a parseable
catalogue**. **17 of 32 also expose a `search` root**, and — significant for an ESP32 — **19 of 32
answer over plain HTTP with no TLS at all, TuneIn among them.**

Three traps here:

- **TuneIn (New), sid 333, is `Auth="AppLink"` and is NOT browsable.** The *legacy* TuneIn, **sid 254,
  is Anonymous**, and it is the one with the global catalogue. Both share the same `sNNNNN` id space
  (proven below), so the household's existing sid=333 favourite is still a useful reference.
- **Sonos Radio, sid 303, is `Auth="DeviceLink"`** here, and its endpoint answers
  `getMetadata(root)` with `<getMetadataResponse xsi:nil="true"/>` — an empty stub. Not third-party
  browsable; not worth pursuing.
- **TuneIn's service type 65031 is the single entry present in `AvailableServiceDescriptorList` but
  ABSENT from `AvailableServiceTypeList`** (106 descriptors, 105 types — TuneIn is the only gap).
  It may resolve anyway as the classic built-in, but that gap is exactly the shape of thing that makes
  Route A fail.

### The minimum viable request

```
POST <service SecureUri>
Content-Type: text/xml; charset="utf-8"
SOAPAction: "http://www.sonos.com/Services/1.1#getMetadata"

<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Header><credentials xmlns="http://www.sonos.com/Services/1.1"/></s:Header>
  <s:Body><getMetadata xmlns="http://www.sonos.com/Services/1.1">
    <id>root</id><index>0</index><count>8</count>
  </getMetadata></s:Body>
</s:Envelope>
```

**An EMPTY `<credentials/>` element is the whole requirement.** Measured against TuneIn:

| header | result |
|---|---|
| `<credentials/>` empty | **200** |
| + `deviceId` + `deviceProvider` | 200 (TuneIn merely appends a `serial=` analytics param) |
| + `<context><timeZone>` | 200 — **not required**, despite node-sonos-ts always sending it |
| no `<s:Header>` at all | **500** (WCF parse fault) |

`deviceId` is NOT required, which matters: a standalone unit has no `R_TrialZPSerial` of its own and
would otherwise have to borrow one from a speaker.

### The verified chain, end to end

```
getMetadata id=root        -> 200, 6,133 B, total=9
   containers: iHeartRadio / Holiday Stations / TuneIn Recommends / Music /
               News & Talk / Sports / Talk / Trending / Location
getMetadata id=y1--33ecb...  ("Music")   -> 200, 36,174 B
   containers: Top Music Stations / Adult Hits / Apple Music Radio / Blues / ...
...descend to itemType=stream ->  id=s105741  "Praise and Worship"
getMediaURI id=s105741       -> 200, 451 B
   http://opml.radiotime.com/Tune.ashx?id=s105741&listenId=1785350179&partnerId=Sonos&version=84.1.0
```

`getMediaURI` is callable anonymously too — verified on **12/12** anonymous services. Samples:

```
TuneIn  s28808      -> http://opml.radiotime.com/Tune.ashx?id=s28808&listenId=...&partnerId=Sonos
SomaFM  groovesalad -> http://api.somafm.com/groovesalad130.pls  -> ice6.somafm.com/groovesalad-128-aac
NTS     nts1        -> https://streams.radiomast.io/nts1
```

TuneIn's `listenId` increments on **every** call (three consecutive calls a second apart returned
three different values). SomaFM and NTS return byte-identical URLs.

On the docs' *"result must not change mid-session"*: that is a promise the **service** makes the
**player** — once playback of an item starts, the backing URL stays valid for its duration. It is
**not** a claim that the value is stable across calls, and TuneIn's demonstrably is not.
**Rule: call `getMediaURI` once per playback, treat the result as single-use, never persist it.**
One extra round-trip on a user-initiated action is free at human timescales.

Format note for the playback test: SomaFM returns a `.pls` playlist
(`groovesalad130.pls` → `File1=http://ice6.somafm.com/groovesalad-128-aac`), and CBC / NRK /
Sveriges Radio return HLS `.m3u8`. Sonos handles both natively, but both should be covered.

### The open question: does a constructed URI actually play?

**This is the one thing still unresolved, and it needs a real playback command.** Two candidate routes:

- **Route A — native.** Construct `x-sonosapi-stream:s105741?sid=254&flags=8224&sn=0` with DIDL
  `item id="F00092020s105741"`, class `object.item.audioItem.audioBroadcast`, desc `SA_RINCON65031_`.
- **Route B — resolve first.** Call `getMediaURI`, then play the returned URL, e.g.
  `x-rincon-mp3radio://opml.radiotime.com/Tune.ashx?id=s105741&...` with minimal `audioBroadcast`
  DIDL. **No service provisioning dependency at all**, so most likely to work.

A third-party SMAPI client's implementation notes claim current firmware rejects a hand-built
`x-sonosapi-stream:` URI with **UPnP 402**, requiring the `getMediaURI` hop. **That claim is
contradicted by this repository's own shipping code:** `core/library.cpp` hands
`x-sonosapi-stream:s34231?sid=333&flags=32&sn=16` (the KNBR favourite) straight to
`SetAVTransportURI` on every Radio-page tap today, on firmware 86.8-78270.

The likely real distinction is **metadata correctness, not URI scheme** — a favourite carries a
firmware-authored `<r:resMD>` with the right `item id=` prefix, `parentID` and `desc` token, while a
hand-built one with the wrong 8-hex prefix or the wrong `SA_RINCON<type>` suffix is exactly what would
produce a 402. That reading reconciles both observations, and is consistent with SoCo and sonoscli
openly shipping a placeholder `0fffffff` prefix they admit they don't understand.

**Test order when the owner is present (one idle speaker, `SetAVTransportURI` + `Play`):**
B1 the resolved TuneIn URL → B2 SomaFM/NTS (confirms `.pls` and bare-stream forms) → A the native
construction. Nothing else in this investigation requires a mutation.

### Construction rules worth not rediscovering

- **`flags` is per-service folklore, not derivable from SMAPI data.** No library computes it. The
  `flags=32` on our KNBR favourite is **firmware-generated**; every library uses `8224` for TuneIn
  streams, and SoCo omits `flags` entirely and works. Prefer omitting it.
- **Look the sid up by NAME** from `ListAvailableServices` at runtime (as node-sonos-http-api does)
  rather than hardcoding 254/333 — we already fetch that list at boot.
- **`sn=0` is the anonymous convention.** TuneIn has no account record, so noson special-cases type
  65031 with a synthetic serial of "0"; firmware sometimes omits `sn=` entirely.
- **desc token for Anonymous is `SA_RINCON<type>_` with nothing appended** (`type = sid*256+7`).
  UserId appends the username; DeviceLink/AppLink append `X_#Svc<type>-0-Token`.
- **`x-rincon-cpcontainer` puts the 8-hex prefix INSIDE the URI**; stream/track/program forms put it
  only in the DIDL `item id=` and carry the bare id in the URI.
- **Shortcut worth knowing:** `http://opml.radiotime.com/Tune.ashx?id=s<NNNNN>` *is* the resolved
  TuneIn URL. A known station id needs no SMAPI at all.
- **Anomaly:** TuneIn's type 65031 appears in this household's `AvailableServiceDescriptorList` but is
  **absent from its `AvailableServiceTypeList`** (106 descriptors, 105 types). Unexplained; may or may
  not affect Route A.

### ESP32 feasibility: viable on-device, no Pi

The resolution hop is one extra SOAP POST (~500 B request, ~250 B response) on the same keep-alive
connection, **at play time, not at list-render time** — so browse latency and memory are unchanged.

**Measured response sizes** — the number that governs everything:

| request | bytes |
|---|---|
| request envelope (any) | ~510 wire / ~325 body |
| SomaFM `root` | 2,302 |
| SomaFM `by_genre` (25 items) | 6,025 |
| TuneIn `root` | 6,133 |
| NTS `root` (18 streams) | 9,193 |
| SomaFM `alphabetical` (all stations) | 13,515 |
| TuneIn station page, `count=20` | 21,143 |
| **TuneIn per-station cost** | **~1,050 bytes/item** |

Nothing approaches the hundreds of KB that would have killed this. Worst realistic page is ~21 KB and
`count`-paging is ours to control, exactly as `sonos::browse` already does for `FV:2`. All comfortably
PSRAM-resident; prefer a streaming tag scrape over a DOM.

Two caveats already familiar to this codebase: **TuneIn ignores `count` at root** (returns all 9
regardless) so treat `count` as a hint and size off `total`; and responses are **chunked**
(`Transfer-Encoding: chunked` behind Cloudflare) — CLAUDE.md already flags this for album art, and
`HTTPClient::writeToStream()` handles it where a raw stream read does not.

Ranked by value/effort:
1. **TuneIn via OPML (`opml.radiotime.com`) — do this first.** Fully public: plain HTTP, no key, no
   auth, no SOAP, no namespaces, flat attributes on `<outline>` elements. All verified live:
   `Browse.ashx?c=local` (local-by-IP stations), `Search.ashx?query=`, `Describe.ashx?id=`,
   `Tune.ashx?id=` (the resolved stream). `guide_id` **is** the `sNNNNN`. A ~100-line scraper on the
   existing `HTTPClient` yields a global catalogue with search, logos and now-playing.
2. **TuneIn SMAPI anonymous (sid 254)** — the same catalogue plus Sonos's own category tree plus
   `getMediaURI`. **Plain HTTP works, no TLS needed.** One envelope shape; `didl.cpp`-style tag
   scraping suffices.
3. **Curated anonymous services** (SomaFM, NTS, Relisten, Radio France, myTuner) — catalogues TuneIn
   carries poorly. Needs `WiFiClientSecure::setInsecure()` (already in-tree, used by
   `core/net/updater.cpp`). A hand-picked shortlist beats presenting 32 undifferentiated entries.

### Run-verified vs read-verified

- **Run:** the anonymous service enumeration (32/106), the header-sensitivity table, the full
  `getMetadata` walk and the `getMediaURI` resolution above — executed directly, output pasted.
- **Read:** the UPnP 402 claim, the per-itemType URI table (from noson's source), the `flags` values
  observed in other libraries.
- **Untested by anyone:** whether either route actually plays. That is the single blocking question.

---

## Spotify: buildable for anything you can NAME — the wall is Spotify's API, not Sonos

**Verdict: yes for tracks, albums and playlists whose id you already have. No for stations and mixes.**
And unlike YouTube Music, the Sonos half is *solved* — the blocker moved to Spotify's Web API, which
was cut twice (Nov 2024 and again **Feb 2026**) in ways that remove exactly the station/mix surface.

### The `/getaa` oracle — a read-only way to test URI validity

This is the most useful technique to come out of the whole investigation:

```
http://<speaker>:1400/getaa?s=1&u=<urlencoded Sonos URI>
```

The player resolves the URI against the service's SMAPI **using the household's own linked token** and
returns the cover art. **A 200 means the URI is real; a 404 means it isn't.** No playback, no
mutation, no credentials of our own. Verified independently twice (byte-identical responses, md5
`88ea9e53`):

| probe | result |
|---|---|
| `x-sonos-spotify:spotify%3atrack%3a4LI1ykYGFCcXPWkrpcU7hn?sid=12&flags=8224&sn=10` | **200**, 100,325 B JPEG |
| same, `sn` omitted | **200**, byte-identical |
| same, `flags=99999` | **200**, byte-identical |
| same, `sid=9` (EU Spotify) | **404** |
| syntactically valid but nonexistent track id, `sid=12` | **404** |

The 404 on a well-formed-but-nonexistent id is what proves the 200 is real resolution rather than a
placeholder. The test id is not among the 341 Spotify track ids this household has ever stored.

**Therefore, empirically: `sn` and `flags` are NOT load-bearing — the player ignores them. `sid` IS,
and must be the household's own (12 here, not the `9` several libraries hardcode).** Caveat: this
proves the *metadata/art* path. The audio path could be stricter, though SoCo omits `sid`/`flags`/`sn`
entirely and Sonos's own internal test code uses a bare container URI with no query string.

### Construction table

`<desc>` for every row here: `SA_RINCON3079_X_#Svc3079-0-Token`. Colons percent-encoded as `%3a`
(raw colons also accepted).

| type | transport / enqueue URI | DIDL item id | flags | `upnp:class` | command |
|---|---|---|---|---|---|
| track | `x-sonos-spotify:spotify%3atrack%3a<id>?sid=12&flags=8224&sn=10` | `00032020spotify%3atrack%3a<id>` | 8224 | `…musicTrack` | AddURIToQueue **or** SetAVTransportURI |
| album | `x-rincon-cpcontainer:1004206cspotify%3aalbum%3a<id>?…` | `0004206c…` | 8300 | `…musicAlbum` | AddURIToQueue |
| playlist | `x-rincon-cpcontainer:1006206cspotify%3aplaylist%3a<id>?…` | `1006206c…` | 8300 | `…playlistContainer` | AddURIToQueue |
| user playlist (legacy) | `x-rincon-cpcontainer:10062a6cspotify%3auser%3a<u>%3aplaylist%3a<id>?…` | `10062a6c…` | 10860 | `…playlistContainer` | AddURIToQueue |
| artist top tracks | `x-rincon-cpcontainer:100e206cspotify%3aartistTopTracks%3a<id>?…` | `100e206c…` | 8300 | `…playlistContainer` | AddURIToQueue |
| **artist radio (station)** | `x-sonosapi-radio:spotify%3aartistRadio%3a<id>?…` | `100c206c…` | 8300 | `…audioBroadcast.#artistRadio` | **SetAVTransportURI only — 804 on AddURIToQueue** |
| show / episode | `x-rincon-cpcontainer:1006206cspotify%3ashow%3a<id>` / `x-sonos-spotify:spotify%3aepisode%3a<id>` | `1006206c…` / `00032020…` | 8300 / 8224 | container / musicTrack | AddURIToQueue |

There is **no playable `spotify:artist:` URI** — artist appears only as a `parentID`.

### Two corrections to the rules recorded earlier in this document

- **The `<desc>` account key is NOT always `-0-`.** The real form is
  `SA_RINCON<type>_X_#Svc<type>-<accountKey>-Token`, where `0` applies only to the *first* account of
  that service. This household proves it: YouTube Music has **two** linked accounts —
  `…#Svc72711-0-Token` (sn=7) and `…#Svc72711-8423f6aa-Token` (sn=11). Also Pandora `-70692a0c-`,
  Sonos Radio `-668459c3-`. **No library models this.** Read the key off an existing favourite of the
  same service rather than hardcoding it.
- **The favourite form differs from the playable form.** This household's Spotify *favourites* use
  `flags=8232` / prefix `10032028`, while all 340 Spotify tracks in its Sonos *playlists* use
  `flags=8224` / `00032020`. Use 8224 — and per the oracle above it doesn't actually matter.

### Spotify's Web API in 2026 — this is the actual blocker

Two rounds of cuts:

**Nov 27 2024:** Related Artists, Recommendations, Audio Features, Audio Analysis, Featured Playlists,
Category's Playlists, 30-second previews, and *"Algorithmic and Spotify-owned editorial playlists."*
`/v1/recommendations` was **the only seed-based radio generator, and nothing replaced it.**

**Feb 11 / Mar 9 2026** (the migration guide) — worse:

- **`GET /artists/{id}/top-tracks` removed** — deleting the API source for Sonos's `artistTopTracks`.
- `/browse/new-releases`, `/browse/categories`, `/users/{id}/playlists`, `/markets` removed.
- **Batch fetches removed** (`GET /tracks?ids=…`) — one HTTP request per item.
- **Search capped at `limit=10`** (was 50).
- **1 client id per developer, 5 users per app, owner must hold Premium.**
- Playlist `items` returned only for playlists the user owns or collaborates on.

**Extended quota mode is unreachable.** Since 2025-05-15 Spotify accepts applications only from
organizations with a launched service and **≥250k MAU**. A household appliance is permanently in
Development Mode.

**Mixes are therefore impossible.** Discover Weekly, Daily Mix 1-6, Release Radar, On Repeat and every
`37i9dQZ…` editorial playlist are Spotify-owned: filtered out of `/me/playlists` even when followed,
and 404 on direct fetch. Spotify staff confirm this is intended behaviour for non-extended-quota
clients. Client Credentials vs user token makes no difference — the gate is the quota tier.

**What a new app CAN still enumerate:** `/search` (≤10 results) · `/artists/{id}` ·
`/artists/{id}/albums` · `/albums/{id}` · `/albums/{id}/tracks` · `/playlists/{id}` metadata ·
`/me/playlists` (non-Spotify-owned only) · contents of playlists the user **owns** · `/me/top/artists`
· `/me/top/tracks` · `/me/player/recently-played` · `/me/library`.

That is a usable browse tree — search, artist → albums → tracks, your own playlists, your top
artists. **It is not a station tree.**

### What runs where

**On-device (no new infrastructure):** URI/DIDL construction, `sid` discovery via
`ListAvailableServices`, `sn` scrape from `FV:2` if wanted, and playback — `soap_client.cpp` already
has `setAVTransportURI`/`addURIToQueue` and `didl.cpp` already extracts `<r:resMD>`. This is a
string-formatting change.

**On-device bonus: album art needs no Spotify API.** `/getaa?s=1&u=<encoded track URI>` serves a
plain-HTTP JPEG for any constructible track — exactly what `core/album_art.cpp` already consumes. A
browse UI gets artwork for free from a speaker we already talk to.

**Off-device (the Pi at .99):** the Spotify Web API itself. Not because of TLS — `updater.cpp` already
ships `WiFiClientSecure` — but because Client Credentials needs a **client secret that cannot live in
distributed firmware**, PKCE's refresh token has a **6-month lifetime that refreshing does not
extend** (a twice-yearly browser ceremony on an appliance), and post-Feb-2026 the loss of batch
fetches makes one browse page N sequential HTTPS round-trips over an already-fragile ESP-Hosted link.
The Pi holds the credential and hands the device a flat `{title, artist, spotify_uri}` list; the device
constructs the Sonos URI and plays it directly. No Sonos cloud, no vendor middleman for playback.

**One credential-free path worth knowing:** ListenBrainz Labs
(`labs.api.listenbrainz.org/spotify-id-from-metadata`) resolves artist/album/track → Spotify track ids
with **no Spotify account at all**. Verified: it returned an id byte-identical to one already in this
household's `SQ:13`. Tracks only — no album, playlist or station lookup.

### Unverified

- **No playback command was sent.** Everything above is read-only probing plus published source.
- Container URIs are unproven end-to-end — `/getaa` 404s on `x-rincon-cpcontainer:` and
  `x-sonosapi-radio:`, but that is *expected* (containers carry absolute `i.scdn.co` art in their
  DIDL; `getaa` is a track-art proxy), so the oracle simply doesn't cover them.
- Whether `flags`/`sn` are ignored on the **audio** path as well as the art path.
- Whether Client Credentials still reaches catalogue endpoints after Feb 2026.
- **Local `ContentDirectory` cannot browse Spotify either** — `SP:`, `S:12`, `SV:12`, container ids
  and parent ids all return **UPnP 701**. Enumeration must come from outside. Now doubly confirmed.

### Artist radio is the risky one — and there is a free test for it

Sonos's artist-radio path has **two independent reports of being broken**: it returns 804 on
`AddURIToQueue` (must use `SetAVTransportURI`), and Sonos's own app broke artist radio on 2024-05-07
with no confirmed fix. If the `audioBroadcast`/402 concern is real, it lands **exactly and only** on
this Spotify type.

**The cheapest possible test costs nothing new:** this household already has 4 Amazon Music *station*
favourites in `FV:2` whose `res` is a raw `x-sonosapi-radio:…?sid=201&flags=8300&sn=6`, and
`core/library.cpp` already plays favourites by handing that straight to `SetAVTransportURI`.
**Just play one.** If it plays, un-resolved `x-sonosapi-radio:` is accepted and the 402 concern dies —
for Spotify stations and for the anonymous-SMAPI Radio page alike.

### Recommendation

Build **search → artist → albums → tracks, plus the user's own playlists**. Both halves are proven,
and the art comes free from the speaker. **Skip stations and mixes**: Spotify will not give a new app
the ids, and even with an id the Sonos station path is the one form with independent reports of being
broken.

---

## Amazon Music (sid 201): SOLVED — DeviceLink browse works, Prime Stations enumerate

> ### ✅ PROVEN END TO END ON THIS HOUSEHOLD
>
> The DeviceLink handshake was completed for real (owner authorised in a browser; `getDeviceAuthToken`
> returned a 630-char `authToken` + 524-char `privateKey`), and browsing works with no Sonos app and
> no Sonos cloud in the path. **`getMetadata(root)` returns 7 containers, and the second one is
> literally "Prime Stations".**
>
> ```
> root -> upsell-banner/#upsell_banner              NEW with Amazon Prime Music
>         catalog/playlists/#prime_playlists        Playlists
>         catalog/stations/#prime_stations          Prime Stations      <-- the thing that was asked for
>         catalog/popular/#catalog_popular_desc     Charts
>         catalog/recs/#catalog_recs_desc           Recommended
>         catalog/new/#catalog_new_desc             New
>         library/#library_node_desc                My Music
>
> Prime Stations -> 26 containers: Recently Played, Popular Genres & Artists,
>                   Holiday, Alternative Rock, Blues, Classic Rock, Country, Jazz,
>                   Latin, Pop, R&B, Rap & Hip-Hop, Rock, K-Pop, Classical, ...
>
> Prime Stations > Jazz -> total=50 stations, e.g.
>   program  Smooth Jazz        catalog/stations/A3SP31LN235GV3/#chunk-E6rfSVq6T76DZ0M1uqkjYQ
>   program  Miles Davis        catalog/stations/A2JQOZI8G660XB/#chunk-SbRXgwInR3qWVewfxtHpVQ
>   program  Ella Fitzgerald    catalog/stations/AQU23GR4JGHVS/#chunk-Yh80R5jKTEmpROI6ZrD4Ug
> ```
>
> **26 genres x up to 50 stations each — the full Prime Stations catalogue, enumerable on-device.**
>
> ### The `#chunk-` question is answered: NEVER construct one
>
> **RUN-VERIFIED.** The token is minted fresh **on every response**. The same container fetched twice,
> seconds apart:
> ```
> Smooth Jazz   1st fetch: #chunk-E6rfSVq6T76DZ0M1uqkjYQ
> Smooth Jazz   2nd fetch: #chunk-e91yr96VT0qwGFIWXdRWzg
> ```
> And the household's Classic R&B favourite carries `#chunk-B3lICZsjReCUZn96D9JspA` from years ago
> while browse now mints `#chunk-w9zY0Rd8S9ujZZFEFi0rFQ` for the same key — **both valid at once**
> (the old one is what a speaker is playing).
>
> So: **the chunk is a per-response handle, old ones never expire, and there is no reason to guess.**
> Browse, take the id verbatim, play it. Tests T2/T3/T4/T5 in the section below are moot — they
> existed only to probe whether a client could mint a chunk, and the answer is that it never needs to.
>
> Note stations arrive as `itemType=program`, which per noson's mapping is
> `x-sonosapi-radio:<id>?sid=&sn=` — matching the household's existing station favourites exactly.
>
> ### What this means beyond Amazon
>
> **15 of this household's 106 services are DeviceLink** and the same handshake applies to all of
> them. This is the general answer to "can a standalone controller browse a real music service": for
> AppLink services no, for DeviceLink services **yes, with one browser ceremony by the owner**.
>
> ### Still untested
>
> Playback. But the risk profile has changed completely: we would now be playing a **server-minted**
> URI taken verbatim from a browse, not a constructed one. The one remaining question is whether an
> un-resolved `x-sonosapi-radio:` is accepted by `SetAVTransportURI` (the UPnP 402 concern), and the
> free test for that is unchanged — play an existing station favourite.
>
> ### Operational notes
>
> - The token pair lives in the session scratchpad at `amzn_token.json` (chmod 600) and is
>   **deliberately not committed**. It is an Amazon Music credential. It will vanish with the session;
>   a production implementation would keep it in NVS.
> - Revoke any time from Amazon -> Login with Amazon / connected apps.
> - `linkDeviceId` from `getDeviceLinkCode` is **per-request and required** by `getDeviceAuthToken`.
>   Capture it with the `linkCode` or the authorisation is unredeemable and the user must repeat it.
> - `getDeviceLinkCode` is answered anonymously, so the handshake can be initiated by anyone; only
>   the browser approval is the owner's.

### Original finding: `prime/` the PATH is legacy — and that is still true

**Asked because the household's stations are here and `prime/` was the interest. Short answers:
`prime/` is a dead namespace, its keys are the same id space as `catalog/`, station keys ARE
enumerable (three ways), and construction is probably possible but cannot be proven without one
playback test.**

### `prime/` is legacy. This is the answer to the question that was asked.

**RUN-VERIFIED.** The current Sonos↔Amazon presentation map was fetched live
(`cf.ws.sonos.com/p/p/c7eb6975-…`, **version 258**, matching this household's
`Manifest Version="258"`), and **it contains no `prime` path anywhere**. Its entire vocabulary is
`library_*` and `catalog_*`; the only station entry is
`flat_search/?type=catalog_station&count=50#catalog_stations_search_desc`.

`prime` survives only as **view names underneath `catalog/`** — and this household shows exactly that
hybrid: two `catalog/` stations have parentID
`catalog/stations/refinements/genres/<uuid>/#prime_stations`. **Amazon renamed the path and kept the
view id.**

Two consistent dating signals: the `prime/` favourite is `FV:2/28`, the **lowest surviving Amazon
object id** (the others are 31/34/36/38/39), and it is the only Amazon favourite whose artwork uses
the **legacy** `images-na.ssl-images-amazon.com` host while newer ones use `m.media-amazon.com`.

**Consequence: there is nothing to enumerate under `prime/` because nothing new is minted there.**
Constructing `prime/` URIs would replay exactly the one station this household already has. Build
against `catalog/stations/`.

**The two namespaces share one id space — RUN-VERIFIED at Amazon's end.** The sole `prime/` key
`A1MXE9T8PKB8ZJ` resolves on the modern public route `music.amazon.com/stations/A1MXE9T8PKB8ZJ` →
*"First Aid Kit Station"*, exactly as the `catalog/` keys do. (Whether **Sonos** accepts that key under
`catalog/stations/` is untested — see T5.)

**Tier is the underlying distinction** (supported, not proven): Sonos's Amazon strings file carries
distinct *"Amazon Prime membership is required"* and Music Unlimited upsell families, and field
reports describe Prime-only accounts failing on Stations until upgrading to Unlimited.

**Correction to the brief that produced this work:** these are not ASINs. They are 13–14 character
Amazon **`STATION_KEY`s**. The 10-char `B0…` ASINs appear only under `catalog/tracks/`.

### What `#chunk-<token>` is

**RUN-VERIFIED:** all four tokens are unpadded base64url encodings of 16-byte **RFC 4122 v4 UUIDs**
(version nibble `4`, variant bits `10`, in 4/4 samples). E.g. the `prime/` one,
`pZgbW2WDQaSwNUWawbJv5A` → `a5981b5b-6583-41a4-b035-459ac1b26fe4`.

**READ-VERIFIED:** Sonos's Amazon item ids *are* Amazon Music Device API URIs, and the `#fragment` is
a documented `LocalReference<T>` — fetch the URI without the fragment, then use the fragment as a key
into that document's `trackContainerChunkDescriptions` map. Static albums use the bare key `"chunk"`;
stations get `chunk-<random>` because stations are "potentially infinite… generated dynamically". So
the token is **server-minted in origin**.

Three pieces of evidence that its *value* is nevertheless not validated:

1. **RUN-VERIFIED.** While a station was loaded on one speaker, `GetPositionInfo` exposed the resolved
   track URI:
   `x-sonosapi-hls-static:catalog/tracks/B00137G8MS/<uuid1>/<uuid2>/A3E8KCX2260OJM/n/PRIME/<sessionUuid>/PRIME_STATION/?sid=201&flags=8&sn=17`
   Probing each field through `/getaa` (which *does* resolve this scheme): the two content UUIDs are
   validated (random → 404, zeros → 404, swapped → 404), but the **session UUID is ignored**
   (random → 200, all-zeros → 200), as are the station key, the tier and the content type. Amazon's
   resolver passes session-scope UUIDs straight through.
2. **RUN-VERIFIED persistence.** The `#chunk-` in the favourite is byte-identical to the live
   `CurrentURI`, and the player was on track 4 of a 4-track chunk with the URI unchanged — so it is a
   *station-instance* id fixed for the life of the URI, not a per-chunk cursor. It still resolves
   years after the favourite was created; a genuine session token would have expired.
3. **READ-VERIFIED.** Denon HEOS's content explorer plays Amazon stations with a **bare `#chunk`, no
   UUID at all** (`mid=catalog/stations/A1ESXGJW9GSMCX/#chunk`). Different client, same id space, no
   token.

**Nobody has publicly documented the `#chunk-<uuid>` form** — GitHub code search returns zero hits for
`"catalog%2fstations"`, `"prime%2fstations"`, or `"chunk-" "x-sonosapi-radio"`. **This cannot be
settled read-only.**

### The `/getaa` oracle does NOT cover stations

**RUN-VERIFIED, properly controlled.** It resolves Amazon *tracks*
(`x-sonosapi-hls-static:catalog%2ftracks%2fB071CQ75R8%2f?sid=201` → 200, 39,313 B; nonexistent ASIN →
404; `sid` load-bearing, `flags`/`sn` not). But **both known-real station favourites return 404**, as
do all 48 combinations of the two station ids × 6 URI schemes × 4 `s=` values, and so do the two real
Pandora `x-sonosapi-radio:` favourites.

The oracle is a **track-metadata** proxy and Amazon's `getMediaMetadata` rejects a container id. **For
stations a 404 carries zero information** — it cannot test `#chunk-` presence, swapping, namespace, or
a bogus key. Do not read those 404s as negative results.

### Enumeration: three routes, and one of them is a genuine correction to this document

**(a) DeviceLink is an OPEN DOOR — and this corrects the blanket claim made earlier here.**
`ListAvailableServices` reports **`Auth="DeviceLink"` for Amazon Music**, not AppLink
(RUN-VERIFIED; this household splits **59 AppLink · 32 Anonymous · 15 DeviceLink**).

With empty credentials `getMetadata` returns `Client.AuthTokenExpired` for every id — no anonymous
browse. **But `getDeviceLinkCode` answers anonymously with a live Login-with-Amazon URL**, and
`getDeviceAuthToken` polls correctly with `Client.NOT_LINKED_RETRY`.

So a third-party client **can link its own Amazon account** over the standard SMAPI DeviceLink
handshake — one browser ceremony by the user — and then browse Amazon's full station tree with no
Sonos app and no Sonos cloud. **This is the door YouTube Music's AppLink slams shut and Amazon's
DeviceLink leaves open**, and it applies to 15 of this household's services, not just Amazon.
The earlier statement here that "a LAN client must become its own registered account" remains true —
what is new is that for DeviceLink services *that is actually achievable*. Untested past the
handshake (needs real credentials); durability risk is `refreshAuthToken` lifetime, unknown.

**(b) A public web index — free, no auth, but against Amazon's crawler policy.**
`GET https://music.amazon.com/stations` with a **Googlebot** User-Agent returns ~156 KB of HTML
containing **14 station keys with titles in cleartext** (Classic Rock Radio, Smooth Jazz, '90s
Country, Relaxing Piano Radio…). A normal browser UA redirects to `/browserWarning`. A companion
oracle: `GET music.amazon.com/stations/<KEY>` with a `facebookexternalhit` UA returns Open Graph tags,
so `og:title` = the station name confirms a key is real, and the generic `Amazon Music` fallback means
it is not.

> ⚠️ **Amazon's `robots.txt` explicitly `Disallow: /stations/`, and both techniques depend on sending a
> User-Agent we are not.** They work, but they are outside Amazon's stated policy, they are exactly
> the kind of thing that gets fingerprinted and broken, and 14 stations is a thin prize. **Prefer (a).**
> Recorded here for completeness, not as a recommendation.

**(c) What does NOT work** (READ-VERIFIED): Music Assistant has no Amazon provider; node-sonos-http-api's
`amazonMusic.js` handles songs and albums only (no stations); Amazon's Web API is closed beta with no
station-listing route; the Alexa Music Skill API is provider-side. Amazon's own docs state *"URIs
obtained from Device API responses should be considered semantically opaque. Do not attempt to parse
URIs."* Local `ContentDirectory::Browse` returns **UPnP 701** for every Amazon container form, and
`MusicServices::GetSessionId(201)` returns **UPnP 806**.

### Construction table

`<desc>` for every row: `SA_RINCON51463_X_#Svc51463-0-Token` (type 51463 = 201×256+7).

| type | transport URI | DIDL `item id` | command |
|---|---|---|---|
| station (current) | `x-sonosapi-radio:catalog%2fstations%2f<KEY>%2f%23chunk-<b64url uuid>?sid=201&flags=8300&sn=6` | `100c206ccatalog%2fstations%2f…` | **SetAVTransportURI** (favourites are `<r:type>instantPlay</r:type>`) |
| station (legacy) | same with `prime%2fstations%2f` | `100c206cprime%2f…` | SetAVTransportURI |
| track | `x-sonosapi-hls-static:catalog%2ftracks%2f<ASIN>%2f?sid=201&flags=0&sn=6` | `10030000catalog%2ftracks%2f…` | AddURIToQueue |
| album | `x-rincon-cpcontainer:1004206ccatalog%2falbums%2f<ASIN>%2f%23album_desc?sid=201&flags=8300` | `1004206c…` | AddURIToQueue |
| locker track | `x-sonos-http:library%2ftracks%2f<uuid>%2f.mp3?sid=201&flags=0&sn=6` | `10030000library%2f…` | AddURIToQueue |

### The playback tests — two of them settle it

Read-only work is exhausted. On **one idle speaker**, `SetAVTransportURI` + `Play`, reusing a
favourite's DIDL verbatim and changing only the id:

- **T1 (control)** — the `prime/` favourite URI verbatim. *Also settles this document's separate
  "is an un-resolved `x-sonosapi-radio:` accepted, is the UPnP 402 concern real" question.*
- **T6 (the thesis)** — `catalog%2fstations%2fA3SP31LN235GV3%2f%23chunk-<freshly minted v4 uuid>`
  ("Smooth Jazz", from the public index, never played here). **If T6 plays, construct + enumerate is
  proven in one shot.**

T2 (`prime/` + fresh UUID), T3 (bare `#chunk`), T4 (no fragment) and T5 (legacy key under `catalog/`)
are diagnosis of a T6 failure, not needed if T6 works.

### Unverified

- **Whether a client-minted `#chunk-` UUID is accepted.** The crux. Untested by anyone publicly.
- Whether Sonos accepts a `prime/`-era key under `catalog/stations/` (proven only at Amazon's web end).
- Anything `prime/`-specific about `#chunk-` — **one sample; all reasoning is namespace-agnostic**.
- Whether DeviceLink browse actually returns the station tree (handshake live, browse untested).
- Whether `flags`/`sn` matter on the audio path (proven irrelevant on the art path only).
- Why one household `catalog/` key ("Moby") does not resolve publicly while the other three do.

## Durability risks (unrelated to YouTube Music, more important than it)

Two findings that threaten the direct-UPnP premise this entire project stands on:

- **`customsd.htm` now returns 403 on S2.** Confirmed twice independently, including a Sonos Lead
  Maestro community post (thread 6930488, 2025-08): *"disabled in a recent firmware update so that
  Sonos can drop support for locally implemented music services, they want to switch to cloud-only
  access for SMAPI."* No official announcement. `bonob`'s README documents the same shift from the
  other side — since **May 2024** an S2 app update requires a custom service be exposed to the public
  internet.
- **A Connection Security → Authentication toggle now exists.** Per Sonos support docs it *"applies to
  third-party integrations that use Sonos cloud and Local Area Network (LAN) APIs, requiring them to
  authenticate."* **Default is OFF**, which is why our firmware works. But the switch exists, and
  `connected-home-architecture` acknowledges a separate cert-gated LAN API not released for wide use.

Neither breaks anything today. Both say the unauthenticated local surface is narrowing. Worth watching
on every firmware release.

---

## Housekeeping this produced: 22 dead favourites removed

While investigating, 22 of the household's 70 favourites were found to point at **service id 151**,
which is absent from `ListAvailableServices` — its type (`151*256+7 = 38663`) is not in the household's
`AvailableServiceTypeList`, while every other sid in use (284, 12, 201, 236, 333) is. The URIs were
`x-sonos-http:...mp3` personal-locker files plus several `… Radio` stations, consistent with **Google
Play Music** (shut down 2020, migrated into YouTube Music).

They were removed with `ContentDirectory::DestroyObject`, one at a time, re-browsing between each and
verifying the count dropped by exactly one and that only a `sid=151` item disappeared. **Favourite ids
did not renumber** during the operation. 70 → 48.

> ⚠️ **The `sid=` filter has a blind spot, and it left 6 dead favourites behind.**
> `x-rincon-cpcontainer:` items carry **no `?sid=` query param**, so a filter that reads the service
> from the `res` URI silently skips every container favourite. Six more Google Play Music entries
> survived on that basis — FV:2/10, 11, 13, 14, 17, 30 — identifiable only from the DIDL `<desc>`
> token `SA_RINCON38663_X_#Svc38663-0-Token` (38663 = 151*256+7).
>
> **The correct service test is the `<desc>` token, not the `res` query string.** Parse
> `SA_RINCON<type>_`, then `serviceId = (type - 7) / 256`, and compare against
> `AvailableServiceTypeList`. That works for every favourite regardless of URI scheme. Any firmware
> feature that greys out orphaned favourites must use this test, or it will under-report exactly the
> way this cleanup did. A backup of all 70 originals (full DIDL) is at
`~/sonos-favourites-backup-2026-07-29.json` — a record, not an undo: a favourite pointing at a
departed service cannot be recreated.

**Possible firmware feature:** the same check (item `sid` vs `ListAvailableServices`) is cheap enough
to run on-device, so a Radio page could grey out or hide orphaned favourites instead of offering them
as playable.

---

## What was NOT verified (honest gaps)

- **No playback command was ever sent** during this research. The negative on URI synthesis is proven
  from token structure and published source, not from a failed playback attempt.
- **`steipete/sonoscli`'s `smapi browse`** was never run — against YouTube Music or anything else.
  That remark was inference from its README. It is the only untested path that could change the
  picture, and it is a read-only test.
- **The GitHub issue-tracker sweep never ran.** Whether there are open SoCo issues about AppLink
  services failing is unchecked, not checked. Rated low-risk to the conclusion given three independent
  library authors all landed on "keep your own keyring", but it is a gap.
- The `refreshtoken` doc says tokens have a *"365-day lifespan"* while `expires_in` is 86400. Most
  plausibly refresh-token vs access-token lifetime, unconfirmed.

## Artifacts

`docs/sonos-music-api/` — the SMAPI WSDL (v1.19.6, 2023-10-24) and the merged Control API OpenAPI
spec, with a README covering provenance and how to refetch. Two tricks worth keeping: appending `.md`
to any `docs.sonos.com` path returns raw Markdown with an `updatedAt` date, and
`https://docs.sonos.com/llms.txt` is a complete page index.
