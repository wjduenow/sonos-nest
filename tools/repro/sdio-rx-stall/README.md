# esp-hosted-mcu#243 — SDIO streaming RX stall, minimal reproducer

ESP32-P4 host + ESP32-C6 co-processor, esp_hosted **2.12.13**, SDIO **streaming** RX mode.

**The fault.** The host's SDIO RX buffer has to grow to the size of the pending stream, and that
allocation asks for **`MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`**. On the P4 that pool is small. When
the allocation fails, the driver logs

```
W (…) H_SDIO_DRV: RX buffer alloc failed (len=18432); dropping read
```

**once**, and never delivers co-processor→host data again. Host→co-processor keeps working, so
what you see upstairs is only RPC timeouts. Only resetting the co-processor recovers it.

The retry added in 2.12.12 (`0985253`, `pending = true`) does not run: the first pass already
cleared `NEW_PACKET`, so the retry pass falls out at
`if (!(BIT(SDIO_INT_NEW_PACKET) & interrupts)) continue;` before reaching the allocation again,
and then blocks in `_h_sdio_wait_slave_intr(…, HOSTED_BLOCK_MAX)`. That is why the warning appears
exactly once rather than every `SDIO_RX_ALLOC_RETRY_MS`.

This program makes it happen in seconds instead of hours, by holding DMA-capable internal memory
down to a chosen headroom — the state a long-running application reaches on its own — and then
taking a bulk TCP stream.

## The real capture comes first

`captures/2026-09-15-four-stalls.log` is the fault as it happened on the product: four stalls in
~13 minutes of ordinary use, with `PKT_STATS` running, extracted verbatim from a 7,221-line serial
capture. It also contains the same device and traffic **after** the workaround, for comparison. If
that is enough to verify a fix, the program below is unnecessary.

## Build and run

```bash
idf.py set-target esp32p4
idf.py menuconfig          # Reproducer configuration -> Wi-Fi SSID / password
                           # (optional) leave-free KB; 24 KB reproduces with the default queue
idf.py build flash monitor
```

Then, from any machine on the same network, using the IP the device prints at boot:

```bash
python3 send_bulk.py <device ip>
```

`sdkconfig.defaults` pins the settings that matter: streaming RX mode, `MEMPOOL_PREFER_SPIRAM`
left **off** (its default), packet stats every 2 s, and a log ceiling of INFO so the driver's own
warning is compiled in — a default ERROR-only build compiles out the one line that names the
cause. The SDIO pins, 1-bit width and 10 MHz clock are from the board this was found on
(Elecrow CrowPanel Advance P4 7"); change them to match yours. The fault does not depend on bus
width, clock or pin choice.

## What you should see

**Failing** (the point of this program):

```
repro: held 41984 B in 41 blocks; DMA-internal now: free 24576, largest 21504
repro: rx=1048576 B (+349525 B/s = 2.80 Mbit/s)  dma-internal: free 24576 largest 21504
W (61234) H_SDIO_DRV: RX buffer alloc failed (len=30720); dropping read
stats: STA: s2h{in[2427] out[2427]} h2s{…}          <- s2h frozen from here on
repro: rx=3145728 B (+0 B/s = 0.00 Mbit/s)  dma-internal: free 24576 largest 21504
repro: STALL DETECTED — no bytes for 10 s while the sender is still connected.
```

The `stats:` line is the giveaway: `s2h in` stops moving for good while `h2s out` keeps counting.

**Healthy**, for comparison — either raise `REPRO_TARGET_DMA_FREE_KB` well above one full read, or
set `CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y`: `rx=` climbs indefinitely and no warning appears.

## Sizing the two knobs against each other

A full streaming read is `(co-processor SDIO Tx queue size) × 1536` bytes. With the default queue
size of 20 that is 30,720 B, so leaving 24 KB free reproduces it immediately. The relationship is
the same one `docs/sdio.md` §9.3 describes from the other side: lowering the co-processor's Tx
queue size shrinks the largest read, and is one of the two interim mitigations. On the product
this came from, the observed failing reads were 15,360–23,040 B.

## Where this came from

A wall-mounted Sonos controller (ESP32-P4 + C6). Four link deaths were captured on the serial
console in ~13 minutes of ordinary use, every one with this signature, at 34.7/54.1/67.1/83.2 KB
of general internal heap free — **two of them with a 31.7 KB largest general block, larger than the
failing request**, because the pool that ran out is the DMA-capable subset and no ordinary heap
metric shows it.

Shipped workaround: `CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y` plus
`CONFIG_CACHE_L2_CACHE_LINE_64B=y` (the 64 B line is required with it — see esp-hosted-mcu#219).
2 h 52 min of the same traffic afterwards: zero failures, and ~90 KB of internal RAM handed back.
That avoids the failing allocation; it does not fix the retry path.
