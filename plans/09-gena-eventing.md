# Plan 09 — UPnP GENA eventing instead of polling (Now Playing first)

Issue: [#6](https://github.com/wjduenow/sonos-nest/issues/6). Status: **in progress**, jukebox only.

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
