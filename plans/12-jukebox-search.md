# 12 — jukebox Search, and a second source for Radio

**Status: planned, nothing built.** The research this rests on is done and run-verified — see the
**2026-09-04 re-investigation** in
[`plans/08-music-service-integration.md`](08-music-service-integration.md). Read that first; this
document assumes it.

## 0. What this adds

1. **A Search page on the jukebox** — type a query, get tracks / artists / albums / playlists from
   a real music service, tap one to play it. Sixth entry on the nav rail.
2. **A source choice on the Radio page** — Amazon Prime Stations (what ships today) *or* Spotify's
   curated playlists, picked with a segmented control.
3. ~~**A fix for a live bug**: Amazon moved from `DeviceLink` to `AppLink`, so
   `core/amazon.cpp:linkBegin()` cannot create a new link any more.~~ **SHIPPED** — along with
   three more that testing it uncovered; see §5, now a record rather than a plan.
4. **A docs pass** — README and `docs/` are marketing/onboarding material and are already behind
   (they don't mention the jukebox at all). Shipping the screens without updating them is not
   "done"; §9 makes that an explicit gate.

**Not in scope: YouTube Music.** Its SMAPI endpoint is behind a Google API-key gateway that only
Sonos's own app and firmware can pass. Closed, with evidence, in `plans/08`. Don't re-open it here.

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

- **Amazon** — the existing cache, unchanged by this work. Note what it now contains: Amazon
  flattened its station root (plans/08, 2026-09-05), so a crawl returns **100 live stations** in one
  implicit container, and `radio_cache`'s merging swap keeps the **~945 stations from the
  pre-flattening cache** under their original genres. So the grid is ~27 genres: `Stations` (live)
  plus the fossils. Both play; only the live ones can be re-enumerated. This is *within* the Amazon
  source — the archive is not a third segment (asked and answered: service-level toggle only).
- **Spotify** — live browse, no crawl needed. `Charts` (10) · `Popular Playlists` (100) ·
  `Genres and Moods` (63 categories → playlists) · `Made For You` (DJ, daylist, On Repeat, Repeat
  Rewind — personalised to the linked account).

> ⚠️ **Call them playlists, not stations.** Spotify's SMAPI tree has **no station or stream
> itemType** — verified. Its algorithmic stations and mixes are gone at Spotify's end, not Sonos's
> (plans/08). "Made For You" is the closest thing and it is still a playlist. A UI that promises
> stations and delivers playlists is a bug report waiting to happen. With Amazon's browsable
> catalogue down to 100, Spotify is now the larger half of this page, not the garnish.

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

**The dependency worth remembering:** the link fix is what makes a crawl *succeed* again, and a
successful crawl against the flattened tree is what would have wiped the cache. Shipping `439a56d`
without `6598a27` would have destroyed ~1,045 irreplaceable station ids. Any future "just fix the
link" change to a service this crawl depends on should ask the same question first.

**Not verified on hardware:** the merge itself, which needs a real crawl on the device (re-link
Amazon in Settings, then refresh). `6598a27` means a failed crawl keeps what is there, and
`f34c5cc` means a failed swap restores it.

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

1. **Spotify track from search** — `SetAVTransportURI` + `Play` with the `x-sonos-spotify:` form
   above. Highest confidence: the household has a working favourite of exactly this shape.
2. **Amazon station from the Radio page** — still untested from plans/08, and free to do now.
3. **Spotify playlist/album container** — only after 1 passes, and only with a captured sample of
   the container prefix for that type.
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

**Phase 1 — plumbing (no UI)**
1. Measure the flash delta of two new core files on `sleep-button`; decide `core/services/` or not.
2. `core/smapi.{h,cpp}` — lift transport + XML helpers + the AppLink link state machine out of
   `core/amazon.cpp`. `amazon` keeps its public API and its crawl.
3. ~~Fix Amazon's ceremony onto `getAppLink` (§5). Re-link with the owner.~~ **DONE**, plus the
   empty-index guard, the flat-root read and the merging swap — §5. One thing is still owed:
   **run a real crawl on the device** (re-link Amazon in Settings, refresh) and confirm the merge
   publishes ~27 genres rather than 1, i.e. that the ~945 fossils survived.
4. `core/spotify.{h,cpp}` + NVS token + presentation-map category ids (§2.3).
5. `settingsSetSpotifyAuth()` / `settingsRadioSource()` in `core/settings.cpp`.

**Phase 2 — linking UX**
6. Settings rows + QR modal + the 5-minute countdown and auto re-mint (§6).

**Phase 3 — Search page**
7. Fourth Lucide glyph; `PAGE_SEARCH` in the rail; page scaffold.
8. Query field + category chips + async search + result rows + art.
9. Track playback (§7 test 1).

**Phase 4 — Radio second source**
10. Source segmented control, Spotify browse lists, persisted choice, disabled-state hint.
11. Container playback if §7 test 3 passes; otherwise tracks-only and say so.

**Phase 5 — ship**
12. §9 in full: README, jukebox guide, screenshots, CLAUDE.md, plans/08 results.

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
- **Container URI prefixes** for Spotify albums/playlists — derivable only from a captured sample
  per type (§3.4).
- **Whether Spotify's Radio lists need a disk cache** — measure before building one (§4).
- **The `catalog` namespace on the player's local Control API** (plans/08, re-investigation). It is
  the last unexplored surface that could make *any* service browsable through the player itself,
  which would moot this entire design. Its command names are unknown and commands don't
  prefix-match, so there is no cheap oracle. Worth an afternoon, not a week.
- **Other AppLink services** — Pandora, TuneIn (New), Plex and Audible all answer the same
  ceremony. If §2.3's runtime category mapping holds up, adding one is config, not code. Plex in
  particular would make the jukebox a browser for a local media server.
