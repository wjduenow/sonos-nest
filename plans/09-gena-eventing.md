# Plan 09 — UPnP GENA eventing instead of polling (Now Playing first)

Issue: [#6](https://github.com/wjduenow/sonos-nest/issues/6). Status: **BUILT AND MERGED for the
jukebox, then DISABLED at the flag.** `-DGENA_EVENTS` is commented out in `platformio.ini` as of
`6ef9b70` (PR #27) — inbound NOTIFY bursts turned out to be half the load profile that wedges the
esp_hosted 2.12.11 SDIO link ([#24](https://github.com/wjduenow/sonos-nest/issues/24)), so the
jukebox is back on the 1 Hz poll until host **and** C6 run esp_hosted ≥ 2.12.12
([#26](https://github.com/wjduenow/sonos-nest/issues/26)). See §8. Not enabled on any other unit —
see §2 for why.

Replace the continuous 1 Hz SOAP poll behind Now Playing with UPnP GENA eventing — subscribe once
per coordinator, let Sonos push changes — keeping a slow poll as a backstop.

---

## 1. Measurements (taken 2026-08-08 against the live system, not estimates)

Read-only SUBSCRIBE/UNSUBSCRIBE probe from a LAN host. Sonos granted **`Second-3600`** on every
service — an hour, far more generous than the 15-30 min typical of UPnP devices.

**Idle room, 90 s: ZERO events.** Polling spent ~270 SOAP calls in that window learning nothing.
That is the entire argument in one line.

**Playing room, 150 s** (`AVTransport` + `RenderingControl` only):

| Service | Events | Initial dump | Steady state |
|---|---|---|---|
| AVTransport | 2 | 6,447 B | 5,077 B — one track change at t=83 s |
| RenderingControl | 1 | 1,560 B | — |
| ZoneGroupTopology | 1 | **28,578 B** | — (separate probe; not needed for Now Playing) |

Measured SOAP response sizes for the poll we are replacing: `GetPositionInfo` **1,659 B**,
`GetTransportInfo` **399 B**, `GetVolume` **288 B**.

**Per hour, per screen unit, steady state:**

| | Round trips | Data |
|---|---|---|
| Polling (today) | ~10,800 | ~13 MB |
| GENA | ~17 events + 2 renewals | **~85 KB** |

**~150x less data.** On the jukebox that matters for a reason beyond tidiness: the ESP-Hosted
link dies under sustained load (`rssi=0` while `wifi=3`, unresolved, recovered only by reboot).
Constant polling is exactly the implicated profile.

### Two findings that change the design

**AVTransport does NOT event on playback position.** 83 seconds of playing produced nothing until
the track changed. The progress bar must be interpolated locally from the last known position plus
elapsed time, and reconciled on each event. You keep a timer either way — eventing does not remove
it.

**`ZoneGroupTopology`'s 28.5 KB dump is larger than the sleep-machine's entire free heap.** Now
Playing does not need it. Rooms/grouping would — which makes Rooms the bigger payoff *and* the
bigger cost, not a freebie riding along.

---

## 2. Per-unit feasibility (live heap, measured 2026-08-08)

| Unit | Free | Min-ever | Verdict |
|---|---|---|---|
| **jukebox** (P4) | 98 KB | 74 KB, largest block 45 KB | **Yes** — a 6.5 KB body is comfortable |
| **nest** (S3) | 78 KB | 60 KB | **Probably** — `heapLargest` unknown, needs a health-build OTA first |
| **sleep-machine** | 30 KB | **14.5 KB** | **No** — see below |
| **sleep-button** | 243 KB | 226 KB | Room to spare, but polls every 3 s headless; near-zero benefit |

The sleep-machine's 14.5 KB minimum is already in the range CLAUDE.md documents as fatal: ~15 KB
free was where LWIP could not get socket buffers and the symptom was Sonos `connection refused`.
A transient 6.5 KB body plus an inbound socket lands on that line. It is the unit that would
benefit most from less traffic and the least able to afford the receiver.

---

## 3. The blocker that is not obvious

**`WebServer` cannot deliver a NOTIFY body.** `NOTIFY` *is* method 25 in `http_parser.h`, so the
server recognises and routes it — but `Parsing.cpp:141` reads the request body only for
`POST/PUT/PATCH/DELETE`. A NOTIFY body is silently discarded and `arg("plain")` comes back empty.

This is the same defect class already documented for uploads in `local_stream.cpp`, and the
workaround is the same and already proven in-tree: **own the read loop on a bare `WiFiServer`**,
exactly as the sleep-machine's :8081 upload socket does. Do not try to route this through
`WebServer`.

---

## 4. Design

`src/core/sonos/gena.{h,cpp}` — Sonos protocol, so it sits with `soap_client`/`ssdp`/`didl`.

**Opt-in per env behind `-DGENA_EVENTS`,** collapsing to inline no-ops otherwise. This is a direct
lesson from issue #7: `+<core/>` sweeps every file into every env, so a new core file that assumes
resources the headless button does not have breaks only that env, and only that env is the one
nobody builds by habit. Guarding it means the S3 units pay nothing and cannot break.

- **Listener**: bare `WiFiServer` on its own port + task. Waits for the link itself (boardInit runs
  before appBoot connects). Parses the request line and headers, then **streams the body**,
  scanning for the handful of `val="…"` fields we need rather than buffering 6.5 KB. Answers
  `200 OK` immediately — a slow callback makes Sonos drop the subscription.
- **Subscriptions**: `AVTransport` + `RenderingControl` on the coordinator. Renew at ~half the
  granted timeout, resending the `SID`. A `412 Precondition Failed` means the subscription is gone
  — fall back to a fresh SUBSCRIBE rather than retrying the renew (documented HA failure mode).
- **Re-subscribe when the coordinator moves.** Grouping changes move it, and `processPending`
  already re-discovers and bumps `g_zonesGen` at exactly those points.
- **Writes into the existing `g_player` under `stateLock()`.** No UI change at all — the screens
  already render from it; only the writer changes.
- **Backstop poll stays.** Slowed, not deleted: subscriptions lapse, links drop, speakers reboot.

## 5. Acceptance

- Track/play-state/volume changed in the Sonos app appears on-device in well under a second.
- `soapCalls` in `/api/config` → `.health` grows at a small fraction of today's rate over a long
  uptime; `soapReconnects` stays flat.
- Survives a soak: speaker reboot, group change, device DHCP lease change, Wi-Fi drop — recovers
  without a device reboot.
- `heapFree` / `heapLargest` no worse than today.

---

## 6. What actually shipped, and what it cost

Everything in §4 was built, and everything below was measured on hardware with eventing live. It
is all still true of the code; it is just not compiled in right now (§8).

- **Renewal works.** Confirmed at the 30-minute mark: `renewals=2, resubscribes=0, failures=0`.
  The 412 fallback exists but has never had to fire.
- **The backstop is the SAME poll, just slower** (1 Hz → 15 s). It was briefly a *reduced* poll —
  position only — and that was a real bug: Now Playing went blank because the only thing that
  could repopulate the title was an event. A backstop that cannot reconstruct the whole screen on
  its own guarantees nothing. Measured result: 3.00 SOAP calls/sec → 0.09.
- **Trust is revocable**: eventing must be subscribed AND have delivered at least one event.
  Losing either returns to 1 Hz on its own, so a lapsed subscription costs latency, never
  correctness.

### The bugs, because they share one shape

Eventing gave several fields a SECOND writer, and nearly every bug was that writer overwriting
something better than it had:

| symptom | cause |
|---|---|
| dial ran up, back, up | every `setVolume` provokes an event carrying a level the dial has passed. The poll had a guard since long before eventing; the event path was added without it |
| art vanished on pause | `artUri` assigned unconditionally from metadata that had none |
| art vanished on play | identity compared title AND artist, so a *sparser* description of the same track read as a track change |
| art flickered constantly | `AVTransportURIMetaData` describes the CONTAINER, not the track — fine on a direct Spotify track where they coincide, wrong for every queue-based one |
| art flickered *still* | GENA stored **relative** art URLs. `HTTPClient::begin()` fails silently on those, so 78% of fetches failed and only the poll's absolute URL worked |
| scrubber reset on pause | position zeroed on any event carrying a title, not only a track change |

**The rule: a partial or lower-quality update must never erase what a fuller one established.**
`playerApplyTrack()` in `player_state.h` encodes it — sticky art, title-only identity, an
`authoritative` flag for transport-URI metadata, and position left to the caller.

**The process lesson is sharper.** Three of those were "verified fixed" against
`health.nowPlaying.hasArt`, which reports whether `artUri` is SET — not whether the image ever
loaded. All three verifications passed while the screen kept flickering. Adding
`artFetch/artFail/artClear` counters found the real cause in one pass. Where measurement came
first, the fix landed first time; where symptoms were reasoned about, it took three attempts.

## 7. If you extend this

- **Rooms is the obvious next target** and the payoff is bigger than Now Playing — its per-room
  poll has a ~1.4 s floor that eventing removes entirely. But it needs `ZoneGroupTopology`, whose
  initial dump measured **28.5 KB**, plus a `RenderingControl` subscription per room. That is a
  real memory conversation on this board, not a freebie.
- **The nest is still a maybe.** It needs `heapLargest` read off a live device first (it runs
  firmware predating that field). `core/sonos/gena.*` is device-agnostic; only `-DGENA_EVENTS` and
  a callback listener port are per-env. Note the jukebox's reason for being off does **not** apply
  to the nest — the S3 has no ESP-Hosted bridge to wedge, so the nest port is blocked on heap
  headroom alone.
- **The sleep-machine is a no.** 14.5 KB minimum free heap — see §2.

---

## 8. Why it is switched off on the jukebox (2026-09)

The argument in §1 was that polling is the load profile that kills the ESP-Hosted link. That was
half right. Bisecting the link deaths under
[#24](https://github.com/wjduenow/sonos-nest/issues/24) — commits `0da011f`, `98ca8fb`, `00a46c6`,
landing as `04a0ed0` — showed **inbound** bursts are what the esp_hosted 2.12.11 SDIO host driver
mishandles, and a GENA `NOTIFY` is inbound: a 6.5 KB body the speaker pushes at us, arriving
alongside station tiles and browses. Upstream esp-hosted-mcu #184 names the mechanism (inbound
flow control) and the only workaround offered is pacing inbound reads. Eventing cannot be paced —
the speaker decides when to push.

So the 1 Hz poll, whose steady-state cost eventing was meant to remove, has the one property that
matters on this bridge today: every transfer is **outbound-initiated and small**, and the device
chooses when to make it. `core/net/inbound_gate.h` serialises everything we *can* control; NOTIFY
is not one of those things.

**This is a deferral, not a retraction.** The code is merged, proven on hardware, and costs
nothing while the flag is off (`gena.cpp` compiles to an empty translation unit). Re-enabling is
one line in `platformio.ini` plus a soak, and the trigger is
[#26](https://github.com/wjduenow/sonos-nest/issues/26) landing esp_hosted ≥ 2.12.12 on host and
C6. Re-verify against §5's acceptance list, and watch `health.inbound` timeouts (must stay 0) and
`health.lastReboot` for `netlink` — those are the two readings that would say the burst problem
survived the upgrade.
