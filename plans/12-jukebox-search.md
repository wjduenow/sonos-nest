# 12 — jukebox Search, and a second source for Radio

**Status: SHIPPED and running on hardware** (`feat/spotify-smapi`, jukebox on
`v0.4.2-39-g06460d1`). Written as a plan, kept as a record: §§0-9 are what was designed, §12 is
what building it actually taught, and every "untested" in the original text that has since been
tested says so.

The research it rests on is in
[`plans/08-music-service-integration.md`](08-music-service-integration.md) — read the 2026-09-04
re-investigation first.

## 0. What this adds — all of it now built

1. ~~**A Search page on the jukebox**~~ **SHIPPED.** Sixth rail entry. Two-pane: keyboard and
   category chips left, results right. Search-as-you-type, and tapping a result plays a track or
   descends into a container.
2. ~~**A source choice on the Radio page**~~ **SHIPPED.** Amazon stations or Spotify, one at a
   time, persisted, never intermingled.
3. ~~**A fix for a live bug**~~ **SHIPPED** — Amazon's link ceremony, plus three more bugs testing
   it uncovered. §5.
4. **A docs pass** — still owed. §9 is the gate and it is not met: the README still does not
   mention the jukebox or button-v2.

**Not in scope: YouTube Music.** Its SMAPI endpoint is behind a Google API-key gateway that only
Sonos's own app and firmware can pass. Closed, with evidence, in `plans/08`. Don't re-open it here.

**Verified on hardware, in order of how much they were doubted:**

- Linking Spotify from the device — QR, approve, token in NVS.
- Search returning tracks / artists / albums / playlists, live, in under a second.
- **Playing a searched track — `plans/08`'s long-standing "no playback command was ever sent" is
  closed.**
- Drilling into an artist: 50 items, and the artist's own radio among them.
- Browsing the Spotify root and its containers on the Radio page.
- Artwork on Search and Radio rows.

**Still unverified:** playing an `x-sonosapi-radio:` STATION (artist radio). The URI is inferred
from Amazon's working form plus the shared `itemType=program`; no Spotify station has been played.

---

## 1. The one-paragraph version of why this is now possible

A standalone controller cannot browse a music service through Sonos — the player returns UPnP 701
for every third-party container, and the household's own service tokens are write-only. It has to
become **its own registered account**. Until now this document set said that was possible only for
`DeviceLink` services. That was wrong: an anonymous `getAppLink` returns a live registration URL
and link code from Spotify, Amazon, Pandora, TuneIn (New), Plex and Audible, and the poll leg
answers `NOT_LINKED_RETRY` on all of them. **The Spotify ceremony has been completed end to end and
`search` verified against all four categories.** One browser approval by the owner, once, and the
device holds its own token.

Measured, and the numbers that make this affordable: **~800 B per search result, 0.6–1.0 s per
call** (3.4 KB at count=4, 15.4 KB at count=20). On a board with ~29.6 MB of free PSRAM, for a
user-initiated action, that is nothing.

---

## 2. Architecture

### 2.1 Lift the SMAPI transport out of `core/amazon.cpp` → `core/smapi.{h,cpp}`

`core/amazon.cpp` already contains a correct, hard-won SMAPI client: keep-alive TLS
(`WiFiClientSecure` + `setInsecure()`, one pooled session — 27 connect/handshake/close cycles was a
measured problem), a hand-rolled header/body reader that **never uses `Stream::timedRead()`** (it
busy-waits without yielding and trips the task watchdog on a TLS socket — see CLAUDE.md), XML
escape/unescape, and `tagValue()`.

None of that is Amazon-specific. Extract it:

```
core/smapi.h / .cpp        SOAP envelope + <credentials> header + pooled TLS post + tagValue/escape
                           + the link-ceremony state machine (getAppLink → poll → token)
core/spotify.h / .cpp      sid 12: root/browse/search, id→URI+DIDL construction, NVS token
core/amazon.h / .cpp       unchanged public API; transport and link ceremony move to smapi::
```

Deliberately **not** a grand refactor: `amazon.cpp`'s crawl, `#chunk-` handling and
`radio_cache.cpp` stay exactly as they are. The Radio page works; the only reason Amazon's link
code changes at all is that it is broken (§5).

> ⚠️ **`+<core/>` sweeps every core file into every env**, and the button envs override `lib_deps`
> to ArduinoJson alone. Nothing here may touch LVGL or TJpg — that is the `core/ui/` rule and it
> applies to `smapi`/`spotify` too. It is also the reason step 1 of §10 is *measure the flash
> delta on `sleep-button`*: if two more core files cost the 8 MB button real headroom, move
> `amazon` + `radio_cache` + `smapi` + `spotify` into a new **`core/services/`** subtree and drop
> it from the four non-jukebox envs with one line each (`-<core/services/>`), exactly as
> `core/ui/` does. Don't grow a per-file exclusion list.

### 2.2 `core/spotify.h` — the shape

Mirror `core/amazon.h` so the two read alike:

```cpp
namespace spotify {

struct Item {                    // one search hit or one browse row
  String title, subtitle;        // subtitle = artist / owner, may be empty
  String id;                     // native id VERBATIM: "spotify:track:6vLa…" — never rebuild it
  String artUrl;                 // may be empty
  enum class Kind : uint8_t { Track, Artist, Album, Playlist, Container } kind;
};

// --- linking (blocking HTTPS; runs on its own task, driven from the UI by state) ---
bool linked();
void unlink();
void linkStart();
enum class LinkState : uint8_t { Idle, Starting, Waiting, Success, Failed, Expired };
LinkState linkState();
const String &linkUrl();
uint32_t      linkExpiresIn();   // seconds left on the code — see the TTL trap below

// --- reading (blocking; netTask only) ---
bool search(const String &term, Item::Kind cat, std::vector<Item> &out, int count);
bool browse(const String &containerId, std::vector<Item> &out, int index, int count);

// --- playback ---
String playUri (const Item &it);
String playMeta(const Item &it);
}
```

> ⚠️ **Everything in `spotify::` except `linked()`/`linkState()` is blocking HTTPS.** Call it from
> netTask or the link task, never from `uiTick`. The jukebox's UI-freeze failure mode is
> indistinguishable from a dead link (CLAUDE.md, `[health]` heartbeat) and this is exactly how you
> would cause one.

### 2.3 The search category ids are per-service — read them, don't hardcode

`search(id=…)` takes the service's own category id, and it differs per service. From each
service's presentation map (`<PresentationMap type="Search">`):

| our category | Spotify `mappedId` | YouTube Music `mappedId` |
|---|---|---|
| tracks | `track` | `SONGS` |
| artists | `artist` | `ARTISTS` |
| albums | `album` | `ALBUMS` |
| playlists | `playlist` | `PLAYLISTS` |
| all | `all` | `ALL` |

Fetch the manifest → presentation map once at link time and cache the five strings in NVS. That is
~2 KB of parsing that makes the module service-agnostic instead of Spotify-shaped, and it is the
difference between adding Pandora/Plex/TuneIn later as config vs. as code.

### 2.4 Credentials header

```xml
<credentials xmlns="http://www.sonos.com/Services/1.1">
  <deviceId>{our stable link device id}</deviceId>
  <deviceProvider>Sonos</deviceProvider>
  <loginToken>
    <token>{authToken}</token><key>{privateKey}</key><householdId>{our household id}</householdId>
  </loginToken>
</credentials>
```

Token 390 B + key 174 B for Spotify. Store in NVS beside the Amazon pair
(`settingsSetSpotifyAuth()` mirroring `settingsSetAmazonAuth()`). Handle the
`Client.TokenRefreshRequired` fault by calling `refreshAuthToken` and re-persisting — **untested,
and the one durability unknown in the whole design** (§11).

---

## 3. Screen — Search

### 3.1 Where it goes

Sixth rail entry: `PAGE_NOW · PAGE_FAVORITES · PAGE_RADIO · PAGE_SEARCH · PAGE_ROOMS ·
PAGE_SETTINGS`. It fits without touching the geometry — `PAD_TOP 22 + 6 × RAIL_STEP 86 = 538`,
under `SCREEN_H 600`, last button bottom at 524.

> ⚠️ **The rail icon needs a fourth glyph in `lv_font_lucide_28.c`.** LVGL's built-in symbol font
> has no magnifier. Regenerate the subset with Lucide's `search`, and **take its codepoint from
> `lucide-static/font/codepoints.json`, don't guess** — they are PUA and not stable across Lucide
> releases (that warning is already in the file's header). Regeneration command is in there too.

Inserting a page in the middle renumbers `Page`; `s_page[]`, `s_railBtn[]`, `s_railIcon[]`, the
`icons[]`/`iconFonts[]` arrays and every `showPage()` caller are indexed by it. Adding
`PAGE_SEARCH` **after** `PAGE_SETTINGS` in the enum and ordering the rail separately is the
cheaper-looking option and the wrong one — keep the enum in rail order and fix the call sites.

### 3.2 Layout (1024×600, design system tokens as elsewhere)

```
┌ rail ┬────────────────────────────────────────────────────────────────┐
│  ⌕   │  status bar (room · group · wifi)                              │
│      │  ┌──────────────────────────────────────────────┐  [ Spotify ▾]│
│      │  │  search field                            ⌕   │              │
│      │  └──────────────────────────────────────────────┘              │
│      │  ( All ) ( Tracks ) ( Artists ) ( Albums ) ( Playlists )       │
│      │  ┌────┐ Bohemian Rhapsody - Remastered                          │
│      │  │art │ Queen                                          [ ▶ ]    │
│      │  └────┘                                                         │
│      │  … 8 rows, scrollable …                                        │
└──────┴────────────────────────────────────────────────────────────────┘
```

- **Reuse the Radio page's search idiom** — `radioShowSearch()` / `radioRunSearch()` already own a
  text area + `lv_keyboard` on this unit. Same widget vocabulary, same detent/scroll behaviour.
  The difference is that Radio searches a *local file* and this one goes over the network, so the
  interaction has to tolerate a second of latency (below).
- **Category chips** map to §2.3. `All` first, because a jukebox query is usually a song title and
  the user should not have to classify it.
- **Rows are ~1 KB of LVGL pool each** (CLAUDE.md). Fetch `count=8`, page with a "More" row rather
  than rendering 50. 8 rows × ~800 B = 6.4 KB on the wire, ~8 KB of pool.
- **Album art**: reuse `core/ui/art_cache` with the same `artThumbUrl()`-style downscale discipline
  as the Radio tiles. Art is fetched **after** the rows exist and only for visible rows, exactly as
  `radioPaintArt()` does on scroll.

### 3.3 The async contract

Search is a blocking HTTPS call and the UI task must not make it. Mirror the crawl's pattern:

```
uiTask:   spotify::searchStart(term, cat)      -> sets state Running, returns immediately
netTask:  drains it between poll SOAP calls (processPending is already interleaved this way)
uiTask:   polls spotify::searchState(); on Done, reads the results snapshot under stateLock()
```

Show a spinner/skeleton for the ~0.6–1.0 s. **Do not blank the previous results while the new query
runs** — that is the same mistake as the `roomstatus::invalidate()` blanking on the Rooms page
(plans/07), and it reads as the tap being ignored.

### 3.4 Playing a result

`playUri()` / `playMeta()` fill `g_pending.playUri` / `.playMeta`, the same channel the Radio page
already uses (`core/player_state.h:78`), so netTask does `SetAVTransportURI` + `Play` on the
coordinator with no new plumbing.

**Tracks first, containers behind a test.** The household already has a *proven* working example of
the track form — favourite `FV:2/64`:

```
x-sonos-spotify:spotify%3Atrack%3A68U7CGrUcsJQ9PcBxk7oxB?sid=12&flags=8224&sn=<n>
```

so a searched track id drops straight into that shape. Albums/playlists/artists need the
`x-rincon-cpcontainer:<8-hex prefix><id>` form, and **the prefix is the risky part** — plans/08's
wrapper rule (low 4 hex digits of the prefix = the `flags=` value) tells you how to *read* one but
not how to derive it for a type we have no sample of. Build tracks, verify, then do containers with
a real sample captured from a Sonos-app-created favourite of each type.

> ⚠️ **`sn=` is the household's Spotify account serial on the players, not ours.** The speaker
> plays with the account linked in the Sonos app; our token only found the id. Read `sn` the way
> `amazon::adopt()` reads its own — from a discovered speaker — and never assume it matches the
> account we linked.

---

## 4. Screen — Radio, with two sources

Today `PAGE_RADIO` is Amazon stations out of `radiocache` (genres grid → station carousel → A-Z
strip → search). Add a source segmented control above it:

```
  [ Amazon | Spotify ]
```

**The toggle is between SERVICES, and the two never intermingle.** One source is showing at a time,
each owns its own list, and switching is a re-render, not a filter over a blended index. A merged
"all your radio" list would mix two id spaces, two artwork hosts and two playback paths behind rows
that look identical, and the first bug report would be "why did tapping this one do nothing".

- **Amazon** — the existing cache, unchanged by this work: **26 genres, ~1,045 stations**, crawling
  cleanly (verified on hardware 2026-09-05). What to know is that this depends on the *credential*,
  not on Amazon: a token minted today enumerates a flat list of 100 stations and no genres at all
  (plans/08). So **a re-link is a downgrade**, and `radio_cache`'s merging swap is what keeps the
  other ~945 — they still play, they just cannot be enumerated by a new token. If that ever happens
  the grid becomes ~27 genres (`Stations` live, plus the preserved ones), still *within* the Amazon
  source: the archive is not a third segment (asked and answered — service-level toggle only).
- **Spotify** — live browse, no crawl needed. `Charts` (10) · `Popular Playlists` (100) ·
  `Genres and Moods` (63 categories → playlists) · `Made For You` (DJ, daylist, On Repeat, Repeat
  Rewind — personalised to the linked account).

> ⚠️ **CORRECTED — Spotify DOES have stations, one level down.** This section said its tree had no
> station itemType. That came from reading only the ROOT, where there are none. Browse an artist and
> the first child is `spotify:artistRadio:<id>` with **`itemType=program`** — the same item type
> Amazon's Prime Stations use. So artist radio is reachable from any artist, from Search or from
> Radio, and `spotify::playUri()` builds the `x-sonosapi-radio:` form for it.
>
> What remains true: the root's four lists are playlists, and Spotify's own API no longer exposes
> the algorithmic mixes, so anything the ROOT offers should still be labelled a playlist.

Persist the choice in NVS (`settingsRadioSource()`), default Amazon so nothing changes for an
existing device. If a source is not linked, its segment renders disabled with a one-line "Link in
Settings" hint rather than disappearing — a missing control is a support question.

> ⚠️ **`radioFlat()` already exists and interacts with this.** It collapses the genre level when the
> cache holds exactly one container. On a device with the fossil archive that is false (~27
> genres); on a fresh device it is true, so Amazon opens straight onto 100 stations with no Back
> button. The Spotify source needs the same treatment decided explicitly — its top level is four
> real containers, so it does *not* collapse.

Whether Spotify's lists get a `radio_cache`-style disk cache is a **decision to make after
measuring**: 63 categories × one browse each is a crawl-shaped cost, but the top three lists are
one call each and the whole feature may be fine live. Start live, cache only if the taps feel slow.

---

## 5. SHIPPED — Amazon's link ceremony, and the three bugs behind it

Branch `fix/amazon-applink`, flashed to the jukebox and running as `v0.4.2-6-gf34c5cc`. Kept here
because §10's phases depend on it and because the order things were found in is the useful part.

| commit | what |
|---|---|
| `439a56d` | **Link over `getAppLink`.** `getDeviceLinkCode` now answers `500 Server.ServiceUnknownError "Cannot parse null string"` with a valid household id. `getAppLink` mints no `linkDeviceId`, so the client picks its own (`wifiHostname()`) and sends it on every poll. Verified end to end with the owner: approval → token in 33 s. |
| `6598a27` | **Never publish an empty index over a good cache.** `refresh()`'s convergence rule publishes with gaps when a pass makes no progress — right for a dead genre, catastrophic for a changed tree. Guards on `missing == genres.size()` so a fully resumed pass (which legitimately fetches nothing) still publishes. |
| `189c090` | **Read the flat station root.** `genres()` skips `itemType=program` rows and, when none remain, returns one implicit container pointing at the root; `stations()` already filtered on `program`, so the crawl drops from 27 requests to 1. The Radio page collapses its genre level via `radioFlat()`. |
| `f34c5cc` | **Merge instead of replace.** Crawled genres first, then every cached station the crawl did not return, under its original genre. Per-genre rather than one flat list because 1,000+ LVGL rows against a 512 KB pool is a UI freeze; membership by 32-bit FNV-1a hash because 1,000 Strings is ~40 KB where `heapLargest` is ~32 KB. The swap is now crash-safe — aside to `.bak`, restore on failure, recovered at the start of the next crawl. |

**The dependency worth remembering:** `439a56d` makes linking work again, and a device that
re-links gets a credential that enumerates 100 stations where the old one enumerates ~1,045
(plans/08 — the tree is credential-dependent, and the first revision of this plan wrongly read it
as Amazon changing for everyone). So the link fix is exactly what puts the cache at risk, and
`f34c5cc` is what keeps it. Any future "just fix the link" change to a service this crawl depends
on should ask the same question first.

**Verified on hardware 2026-09-05:** a Refresh now on the device crawled all 26 genres (25 fetched,
one transient Jazz failure) and correctly deferred publishing to the next pass. **Still unverified:
the merge**, which only does something once a crawl and the cache disagree — i.e. after a re-link.
`6598a27` means a failed crawl keeps what is there, `f34c5cc` means a failed swap restores it.

Shipped alongside, from the same report: `28ef2d5` — the Settings *Refresh now* button had no
`LV_STATE_PRESSED` style and read as a dead control while working perfectly, and a refresh that
cannot run yet (no storage, no Wi-Fi, no linked account) now says so instead of being held in
silence.

---

## 6. Settings — linking a service

One row per linkable service (Amazon, Spotify), each showing linked/not and a **Link** button that
opens a modal with the URL and a **QR code** — the jukebox is wall-mounted, so typing a URL off it
is not a real option.

> ⚠️ **The link code has a hard 5-minute TTL and it expires silently in the browser.** Measured on
> Spotify: minted 09:29:20, still `NOT_LINKED_RETRY` on the wire at 09:34:05, `Invalid linkCode` at
> 09:34:10 — while the web page had already been saying *"The link code you entered is not valid"*.
> So the modal must show a **countdown**, and on expiry re-mint automatically and redraw the QR
> rather than leaving a dead code on the wall. This cost a failed attempt during the research and
> it will cost every user one if the UI doesn't handle it.

Amazon's window is longer (Sonos's own app polls for ~7 minutes, which is what `amazon.cpp`
already uses) — read the expiry per service, don't share a constant.

---

## 7. Playback test order (owner present, one idle speaker)

Nothing in the research so far has sent a playback command; these are the first.

1. ~~**Spotify track from search**~~ — **PASSED on hardware 2026-09-06.** Tapping a searched track
   plays it. This is the first playback command sent in this entire line of work and it closes the
   "no playback command was ever sent" caveat that had stood in `plans/08` since the beginning.
2. **Amazon station from the Radio page** — still untested from plans/08, and free to do now.
3. ~~**Spotify playlist/album container**~~ — **NOT NEEDED, and this was the wrong plan.**
   Containers do not have to be constructed: they BROWSE. An album returns its tracks, a playlist
   its tracks, an artist its top tracks, its radio and its albums. Drilling down is now what a
   container tap does on both pages, and no `x-rincon-cpcontainer` prefix has to be guessed.
4. **A Spotify STATION** (`x-sonosapi-radio:`, artist radio) — the one playback form still
   untested. Inferred from Amazon's working station URI plus the shared `itemType=program`.
4. **Amazon re-link** end to end via `getAppLink` (§5).

A UPnP **402** on any of these means the DIDL is wrong (prefix / `desc` token), not the URI scheme —
plans/08 §"Construction rules" has the reasoning and the known-good `desc` forms.

---

## 8. Budget and risk

- **PSRAM**: negligible. A 20-row result set is ~15 KB on the wire and a few tens of KB of art.
- **Internal SRAM**: this is the one that bites. mbedTLS is already configured to allocate from
  PSRAM on this env (`MBEDTLS_EXTERNAL_MEM_ALLOC`, `ASYMMETRIC_CONTENT_LEN`) — **that is
  load-bearing, don't revert it**, and a second concurrent TLS session would undo the margin it
  bought. Reuse `smapi::`'s single pooled session; do not open Spotify and Amazon sockets at once.
  Tag the search path with `heapwatch::note("spotify")` so `/api/config → health.heapLow` can
  attribute it.
- **Flash**: two more core files in **every** env (§2.1). Measure `sleep-button` first.
- **LVGL pool**: 8 result rows ≈ 8 KB against a 512 KB pool peaking at ~48 KB. Fine, but free the
  rows on page exit (`lv_obj_clean`) as the browse lists already do.
- **Durability risk**: `refreshAuthToken` lifetime is unknown for both services. If a token dies
  silently the page must say "re-link in Settings", not show an empty list.

---

## 9. Docs and marketing update — a completion gate, not a follow-up

The screens are not done until these are, in the same PR as the feature:

- **`README.md`** — the units table and the feature blurbs. Note it is **already behind**: it lists
  three units and does not mention **sonos-jukebox** or **sonos-button-v2** at all, and the
  architecture tree omits `boards/crowpanel_p4_7in/`, `boards/xiao_esp32s3/`,
  `boards/button_common/` and `units/sonos_jukebox/`. Fix that in the same pass — a reader landing
  on this repo currently cannot tell the jukebox exists.
- **`docs/` per-unit guide for the jukebox** — there are guides for nest and sleep-machine
  (`docs/sonos-nest.md`, `docs/sonos-sleep-machine.md`) and none for the jukebox. The new screens
  are a good reason to write it: screens, linking a music service, the Radio sources, Search.
- **Screenshots** — the portal README sets the precedent (`sonos-portal/docs/dashboard.png`). A
  photo of the Search page and the Radio source switcher on the real panel.
- **`CLAUDE.md`** — one line in the jukebox section naming the Search page and the two Radio
  sources, and a pointer to this plan, in the same style as the existing `plans/` pointers.
- **`plans/08`** — record the playback results from §7 as they land. Its whole value is that the
  negative results are trustworthy; leaving §7 forever "untested" erodes that.

---

## 10. Task list

**Phase 1 — plumbing.** DONE. `core/smapi` extracted (transport, XML helpers, credentials);
`core/spotify` written; NVS token and `settingsRadioSource()` added; Amazon's ceremony fixed (§5).
Step 1 answered with a measurement: both new core files cost **1,340 bytes** on `sleep-button`
(0.04% of its slot), so no `core/services/` subtree is warranted.

**Phase 2 — linking UX.** DONE. A Spotify row in Settings sharing Amazon's QR overlay, with the
5-minute countdown that Spotify's code lifetime demands.

**Phase 3 — Search page.** DONE, and beyond the plan: two-pane layout, a reduced keymap, and
search-as-you-type (§12).

**Phase 4 — Radio second source.** DONE. Segmented control, persisted, Spotify browsed live.
Container playback turned out not to need constructing at all (§7).

**Phase 5 — ship.** NOT DONE, and it is the only phase left:

- §9 in full — README, a jukebox guide in `docs/`, screenshots, CLAUDE.md.
- **Run a real Amazon crawl on the device** (re-link, refresh) to confirm the merging swap
  publishes ~27 genres rather than 1, i.e. that the ~945 fossils survive. Still owed from §5.
- **Play a Spotify station** (§7 test 4).
- Open the PR.

---

## 11. Open questions

- **`refreshAuthToken` lifetime.** Partly answered: Amazon's access token expires in **well under
  an hour** and the replacement arrives inside the `Client.TokenRefreshRequired` fault, which
  `amazon.cpp:request()` already handles. What is still unknown is how long the *refresh* right
  lasts before a re-link is required — for either service. The re-link path must stay reachable
  from the UI.
- **Does the merge actually preserve the fossils on hardware?** Reasoned through and built, never
  run against a real card (§5). It is the one step that can still lose the ~945 stations, and the
  crash-safe swap is the net under it.
- **Does an `x-sonosapi-radio:` station play with `sid=12`?** The last untested playback form
  (§7 test 4). Tracks are proven, so a failure would be isolated to stations.
- **How often does the art path provoke the netlink reboot?** One event, mitigated by pacing and by
  not fetching PNGs, but the underlying ESP-Hosted fault is unresolved upstream and this feature is
  the most network-active thing the jukebox does.
- **Spotify's rate limit is unknown**, and search-as-you-type is the first thing here that makes
  repeated calls. Debounced 400 ms with a three-character floor; if searches start failing after
  fast typing, that is the cause.
- ~~**Container URI prefixes** for Spotify albums/playlists~~ — **moot**. Containers browse into
  tracks; nothing needs constructing (§7).
- **Whether Spotify's Radio lists need a disk cache** — measure before building one (§4).
- **The `catalog` namespace on the player's local Control API** (plans/08, re-investigation). It is
  the last unexplored surface that could make *any* service browsable through the player itself,
  which would moot this entire design. Its command names are unknown and commands don't
  prefix-match, so there is no cheap oracle. Worth an afternoon, not a week.
- **Other AppLink services** — Pandora, TuneIn (New), Plex and Audible all answer the same
  ceremony. If §2.3's runtime category mapping holds up, adding one is config, not code. Plex in
  particular would make the jukebox a browser for a local media server.

---

## 12. What building it taught — the hardware-only bugs

Six faults, none of which a build could have caught, in the order they were found. Every one was
identified from the device rather than by reading code, and the diagnostic that named each is worth
as much as the fix.

**A 6 KB stack against a 15.7 KB response.** The search worker was created with 6144 bytes, copied
from the LINK task — which only ever holds a link code and a token. Search survived on 3-6 KB
responses; the first browse of a real container rebooted the device. The coredump named it: task
`spsearch`, `mcause 5`, and a PC that does not resolve in the ELF, which is what a corrupted return
address looks like. **Every task here that holds a SOAP response and parses it uses 8192** —
`radiocache`, `favcache`, `netTask`, `artTask`, `uiTask`. Measured margin at 8192 afterwards: 5,032
bytes free. The link task was the wrong precedent to copy.

**A diagnostic that turned an out-of-memory into a reboot.** Raising the stack did not fix it, and
the SAME PC in a different build ruled the stack out. `_svfprintf_r`, `a0 = 0`, `a3 = 0x7f7f7f7f`:
a `printf` with a NULL `%s`, faulting in ROM `strlen`. Arduino's `String` calls `invalidate()` when
an allocation fails, setting its buffer to `nullptr`, so `c_str()` returns NULL — and the
`LOG.printf` added to diagnose an empty browse passed that straight to `%s`. **`smapi::cstr()`
exists so this cannot recur**, and it has since caught a second, unrelated NULL (below).

**Two copies of every response.** `readResponse()` ended with `out = raw`, holding two body-sized
buffers at once — 32-48 KB of contiguous internal heap for a 16-22 KB response, on a board whose
largest free block is ~36-43 KB. It also reserved 24 KB *before reading the headers*, so a 1.9 KB
response took 24 KB. Now a move, and a reserve of exactly `Content-Length`. Shared transport code,
so Amazon got the fix too.

**A use-after-free the log caught without a coredump.** `[spotify] browse (null) -> 344 B, fault:
Action not found.` The `(null)` is `cstr()` reporting a dead String: the Radio page held a
REFERENCE into `s_spItems`, and `radioShowSpotify()` clears that vector before building the
request, so an empty id went out and Spotify rejected it. The Search page's equivalent took a copy,
which is exactly why drilling into an artist worked there and not here — same feature, two call
sites, one wrong. Fixed at both ends: the callback copies, and the function takes its strings **by
value**, because a function that clears the container its arguments came from has no business
borrowing them.

**A page that asked the wrong thing about itself.** "Radio worked and then stopped" was the entry
check testing whether the MODEL was empty when what goes blank is the LIST WIDGET. `radioClear()`
runs from several paths and empties the list without touching the vector, so a cleared page looked
"already loaded". It asks the widget now. A related one: the Radio page decided a browse had
arrived by looking at `browseState()`, which the SEARCH page had already left in `Done` — two pages
sharing one request slot, where "has a browse finished" is not "has MY browse finished".

**Provoking the ESP-Hosted link fault.** A blank screen that was not a crash: `resetReason 3`, no
coredump, `health.lastReboot = "netlink"` — the device rebooting itself under the known unresolved
link fault. The art worker had **no pacing at all** and drained its queue back to back, so a screen
of rows was a screen of TLS handshakes; and Spotify's placeholder icons are PNG on a DIFFERENT host
from its artwork, so a mixed list forced a fresh handshake on every alternation against the one
pooled client. PNGs are no longer fetched (they can never decode here) and fetches are paced 120 ms,
as `radio_cache` has always paced its crawl. Neither cures the link fault; both stop provoking it.

**The one that was not a bug at all:** "no art in Search" was not a rendition problem.
`artcache::keyOf()` is Amazon-shaped — it extracts a station key from
`catalog/stations/<KEY>/#chunk-` and returns `""` for anything else — and `artcache::get()` drops
an empty key *before queueing a fetch*. Every Spotify row asked for no artwork at all, which is
also why a rendition rule written the day before appeared to work and had in fact never run. Fix
the call site before the CDN.

---
