# Wake word — app integration (sleep-machine)

Status: **shipped with two phrases.** "Kinder Bedtime" and "Kinder Wake-Up" fire at 0.96–1.00 on
real voice and drive the app hands-free. "Kinder Rise and Shine" is built and vendored but **not
shipped** — it detects unreliably (below). Next step: retrain a shorter **"Kinder Rise"**.

Background: Phase 0 (mic bring-up) and Phase 1 (pipeline + custom training) are in
`plans/01-sonos-knob-controller-plan.md` and CLAUDE.md. This doc covers wiring the proven engine
into the app, and is written so a fresh session can pick up the "Kinder Rise" work.

## What shipped

| Phrase | Action | UI equivalent | Reliability |
|---|---|---|---|
| Kinder Bedtime | Play the Sonos **Sleep playlist**, volume 45 | "Start Sleep Sounds" card (`cloudCb`) | fires 0.96–1.00 |
| Kinder Wake-Up | Stop | Stop button (`stopCb`) | fires 0.96–0.98 |
| ~~Kinder Rise and Shine~~ | Swap in the wake track (streams from SD) | Wake button (`wakeCb`) | **not shipped** — 0.00 on most utterances |

Architecture (unchanged from the HAL contract):
- `boards/es3c28p/wake_word.cpp` reports **which phrase was heard** (`wakeWord*` in `core/board.h`).
- `units/sleep_machine/screens.cpp::handleWakeWord()` decides what it **means**, called from
  `uiTick()`. Boards must not touch `g_pending`/`settings` — hence the split.
- Each phrase calls the same callback as its on-screen button, so voice and touch can't drift.
- `crowpanel_rotary` stubs `wakeWord*` (no mic) — `wakeWordPoll()` returns -1, so `handleWakeWord()`
  is a no-op there.

Cost: **~31 KB internal SRAM**, ~8 ms/inference for 2 models, 2 tasks (core 1), 2×80 KB PSRAM arenas.

## Bugs found during integration (all fixed — don't rediscover)

1. **`TF_LITE_STATIC_MEMORY` must be set on the ENV, not just in `lib/tflm/library.json`.**
   library.json's flags configure only the library's own sources. That macro selects a *different*
   `TfLiteTensor` field order, so a mismatch is not a link error: `AllocateTensors()` returns OK,
   `interp->input(0)` returns a plausible pointer, and every field read through it is garbage
   (`params.scale=-nan`, `dims->data[1]` = 0x55555555 poison) → `StoreProhibited` on first Invoke.
   **Symptom to recognise: valid pointers + nonsense quant params = ABI mismatch, not memory.**
   The `sleep-machine-wake` env always had these flags, which is why the bring-up never hit it.

2. **Internal SRAM is what breaks Sonos first.** The models are nearly free (~228 B each; arenas are
   PSRAM). The cost is I2S buffers and task stacks:
   - `buffer_count=8 / buffer_size=1024` cost **33.8 KB** (audio-tools allocates far past the DMA
     descriptors). `4 / 512` costs **9 KB** with no measurable detection loss (0.96–0.99).
   - Stacks are sized off measured high-water marks (capture ~3.5 KB peak, inference ~2.5 KB).
   - At ~15 KB free (largest block 7.6 KB) LWIP can't get socket buffers. **The symptom was Sonos
     `connection refused` and "File system is not mounted"** — nothing pointing at the wake word.
   - Healthy: ~47 KB free / ~35 KB largest with 2 models.

3. **Wake tasks belong on core 1 — core 0 is the network's.** `netTask` (prio 2) and `media-httpd`
   (prio 1, streams SD → Sonos) live on core 0. Capture must be high-priority (or I2S goes stale),
   so on core 0 it starves them. **This failure only appears while streaming** — an idle device
   polls Sonos fine.

4. **A "does the network work?" control test that isn't playing anything proves nothing.** Several
   hours went into "the wake engine breaks Sonos" using idle controls. Always exercise the
   *streaming* path.

5. **Bedtime was mis-wired to the SD-stream card (`localCb`) instead of the Sonos playlist
   (`cloudCb`).** "Play sleep on Sonos" means the speaker's own Sleep playlist; the SD path is a
   deliberate no-internet fallback. Rise and Shine is the only voice action that streams from SD.

6. **A corrupt incremental build presents as "every model silently scores 0.00"** — not as a compile
   error. This dev machine has a known hardware fault (BIOS update pending; random SIGKILL/ICE under
   load) and threw a GCC ICE and a SCons `TypeError` in one session. **If detection dies after a
   change that shouldn't affect it, `pio run -t clean` before debugging the code.** This cost a
   whole diagnostic cycle. Keep `-j 2` to reduce load.

## Why "Kinder Rise and Shine" didn't ship

Measured with the `WAKE_DEBUG` heartbeat (below), speaking the phrase 4–5× per run, `rms` 10 000–27 000:

- Peak score **0.00 on most utterances**; 0.75 once; 0.96–0.99 on a couple of earlier runs.
- Bedtime/Wake-Up hit 0.96–1.00 on the same runs, same audio → **pipeline is fine, model is weak**.
- Lowering its cutoff to 0.70 **did not help** — the failures are *zeros*, not near-misses. There is
  nothing to lower the bar to. (Per-model `cutoff` stays in `WakeModel`; it's the right mechanism,
  just not the right fix here.)
- Suspected cause: the phrase is long and its cadence varies a lot between utterances, which suits
  the 3-frame streaming-window architecture poorly. Short, punchy phrases work best.
- Note `rms` ~27 000 approaches int16 clipping (32767). "Loud and close" advice may be
  counterproductive at the top end; clipped audio yields garbage features. Not the main cause
  (Bedtime fires at the same levels), but worth controlling for when testing "Kinder Rise".

## Next: "Kinder Rise"

1. Train per `training/wake-word/` (RTX 4090; a run is well under an hour). Same recipe as the other
   three — Piper TTS positives + cross-phrase hard negatives (**include "Kinder Rise and Shine" as a
   negative**, since "Kinder Rise" is a prefix of it).
2. Vendor the `.tflite` + generated `_model.h` into `boards/es3c28p/models/`.
3. Re-add the entry to `s_models[]` in `wake_word.cpp` (cutoff `kProbCutoff` to start).
4. Change the `"Kinder Rise and Shine"` string in `handleWakeWord()` — the branch is dormant but
   intact and tested (streams the wake track; handles both "already playing" and "from idle").
5. Test with `WAKE_DEBUG 1`.

**Do not judge a model by TTS-on-TTS testing** — that grid is circular and produced phantom
cross-fires that cost three training rounds in the previous session. Real voice is the only oracle.

## Testing protocol

Set `#define WAKE_DEBUG 1` in `wake_word.cpp` and rebuild. Every 2 s it prints:

```
[wake] hb rms=13146 peak: 0.99 0.00 0.00      <- mic level, then each model's peak since last beat
```

This separates the two failure modes that look identical from the outside:
- `peak 0.7` → model recognises you, threshold/window problem → tune `cutoff` / `kSlidingWindow`.
- `peak 0.00` → model doesn't recognise you at all → retrain; no threshold will fix it.

`rms` guards against false negatives: **below ~10 000 is too quiet to trust a negative** (this cost
hours in the previous session — "esp-nn miscomputes" was really just quiet tests); **above ~25 000
risks clipping.**

Serial reading on WSL: `~/.platformio/penv/bin/python tools/readser.py /dev/ttyACM0 40`
(the venv python has pyserial; the system python does not).

**Process note:** on-device voice tests need the user to speak. Prompt them and **wait for their
reply** before starting the serial read — don't prompt and listen in the same turn, or you capture
silence and misread it as a negative.
