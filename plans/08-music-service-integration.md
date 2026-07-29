# 08 — music service integration (YouTube Music, SMAPI, the Control API)

**The question:** the household has YouTube Music linked, and YouTube Music has its own radio
stations. Can a unit enumerate them and put them on a Radio page?

**The answer: no, and not through any route.** Verified three independent ways (below). This is a
closed question, not an unexplored one — **do not re-open it without new evidence**, because it cost
three research agents and a lot of tokens to close properly.

**What works instead:** Sonos favourites (`FV:2`). A station favourited once in the Sonos app appears
there as a playable `sid=284` container, which `core/library.cpp` already plays. That is how the 28
YouTube Music favourites on this household got there.

**What is newly possible and worth building:** a *capture* channel. You cannot browse what a service
offers, but you CAN read the id of anything that has played — locally, read-only, no token. See
*The capture channel* below. It is the only genuinely new capability this research produced.

---

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

**Also available and unexplored:** services whose descriptor says `Auth="Anonymous"` can be browsed
directly over SMAPI with no token at all — on-device, no Pi, no cloud. That covers a large slice of
this household's service list (SomaFM, Radio Paradise, AccuRadio…). It does nothing for YouTube Music,
but "browse real radio catalogues on the device" is achievable within the project's premise. The real
`credentials` SOAP header contract is in the WSDL and **differs from the prose docs** (which omit
`sessionId`); trust the WSDL.

---

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
did not renumber** during the operation. 70 → 48. A backup of all 70 originals (full DIDL) is at
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
