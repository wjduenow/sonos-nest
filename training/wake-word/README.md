# Custom "Kinder …" wake-word training (microWakeWord)

Recipe that produced the custom wake-word models in `src/boards/es3c28p/models/kinder_*.tflite`.

Framework: [OHF-Voice/micro-wake-word](https://github.com/OHF-Voice/micro-wake-word) (Apache-2.0).
Runs fully **local and account-free**. The workspace (~35 GB of features/datasets) lives OUTSIDE
the repo at `~/mww-train/`; only the scripts live here.

| Phrase | Spelling fed to Piper | Action | On real voice |
|---|---|---|---|
| Kinder Bedtime | `Kinnder Bedtime.` | Sonos Sleep playlist | **fires 0.96–1.00 — shipped** |
| Kinder Wake-Up | `Kinnder Wake Up.` | stop Sonos | **fires 0.96–1.00 — shipped** |
| Kinder Rise and Shine | `Kinnder Rise and Shine.` | play the wake track | **0.00 on most utterances — NOT shipped** |

> **This recipe is 2-for-3, not 3-for-3.** An earlier version of this file claimed all three were
> "validated on real voice at 0.95–1.00". That was true of Bedtime and Wake-Up and **false** of
> Rise and Shine, which scores 0.00 on most real utterances (0.75–0.99 on a lucky few) while the
> other two hit 0.96–1.00 on the *same audio*. Lowering its cutoff doesn't help — the failures are
> zeros, not near-misses. Best theory: long, cadence-variable phrases suit the 3-frame streaming
> architecture poorly. **Prefer short, punchy phrases.** A shorter "Kinder Rise" is the planned
> replacement — see "Adding a new phrase" below. Details: `plans/03-wake-word-integration.md`.

## Pipeline
1. `setup_env.sh` — conda env `mww`: TF (GPU) + microWakeWord + Piper + pymicro-features.
2. `gen_positives.sh` — Piper TTS, 2000 samples/phrase, 100 speakers.
3. `aug_prep.py` — augmentation sources → 16 kHz (MIT RIR reverb + FMA background noise).
4. `gen_features_only.py <label>` — augmented spectrogram features (the positives).
5. `gen_clean_xneg.py <label>` — *lightly*-augmented features, used as **clean cross-negatives**.
6. `train_phrase.py <label>` — writes the config + trains 10k steps + exports the streaming tflite.
7. `train_all.sh` — every phrase in its `LABELS` list, end to end (needs all xneg features first).
8. `make_model_header.py <label>` — vendor the trained `.tflite` into the firmware as a C header.
   (Verified to reproduce the committed headers byte-for-byte; `alignas(16)` is load-bearing.)

## Adding a new phrase (e.g. "Kinder Rise")

The phrase list is **hardcoded in three places** — there is no single source of truth, so edit all
three or you'll train a model with the wrong negatives and not find out until it's on the device:

1. **`gen_positives.sh`** — add a `gen <label> "<Piper spelling>"` line. Use `Kinnder` (see gotchas).
2. **`train_phrase.py`** (~lines 67 & 70) — add the label to **both** sibling lists. These are the
   cross-phrase hard negatives; each model trains against every *other* phrase in that list.
3. **`train_all.sh`** — add the label to the `for label in …` loop.

**A new phrase that is a prefix/substring of an existing one needs that one as a hard negative.**
"Kinder Rise" is a prefix of "Kinder Rise and Shine", so the longer phrase MUST stay in the sibling
lists in step 2 even though it isn't shipped — otherwise "Kinder Rise" fires halfway through it.

Then, per phrase:
```bash
./gen_positives.sh                     # Piper TTS -> ~/mww-train/work/samples_<label>/
# LISTEN to a few samples before training thousands — verify KIN-der, not KYNE-der.
python gen_features_only.py <label>    # augmented positives
python gen_clean_xneg.py   <label>     # lightly-augmented clean cross-negatives
systemd-run --user --scope python train_phrase.py <label>   # 10k steps, must not be interrupted
```
Vendor it into the firmware:
```bash
cp ~/mww-train/work/trained_models/<label>/tflite_stream_state_internal_quant/\
stream_state_internal_quant.tflite src/boards/es3c28p/models/<label>.tflite
python3 training/wake-word/make_model_header.py <label>     # -> <label>_model.h
```
Then in `src/boards/es3c28p/wake_word.cpp`: `#include "models/<label>_model.h"` and add an entry to
`s_models[]` (start at `kProbCutoff`). Add the matching branch in `handleWakeWord()`
(`units/sleep_machine/screens.cpp`) — for "Kinder Rise" the Rise-and-Shine branch is already there,
dormant; just change the string. Each extra model costs ~228 B internal SRAM + ~4 ms/inference.

**Test on the device with `WAKE_DEBUG 1`** (`wake_word.cpp`) — a 2 s heartbeat of mic rms +
per-model peak. It separates "scored 0.7 → tune the cutoff" from "scored 0.00 → retrain, no
threshold will save it". Keep rms in ~10 000–25 000: below is too quiet to trust a negative, above
risks int16 clipping.

## Hard-won gotchas (these cost real time)
- **"Kinder" must be spelled `Kinnder`** for Piper → `kˈɪndɚ` (German KIN-der). Plain "Kinder"
  gives `kˈaɪndɚ` ("KYNE-der"). Verify phonemes before generating thousands of samples.
- **Cross-phrase hard negatives are required** — without the other two phrases as negatives, a model
  fires on anything starting with "Kinder". Both flavors are used: the augmented positives
  (weight 6) and clean renditions (`features_xneg_*`, weight 12).
- **pymicro-features must be the PyPI build** (`process_samples`), not the puddly fork
  (`ProcessSamples`) — microWakeWord calls the lowercase API.
- **`datasets` must be a soundfile-based version** (pinned 3.2.0). Newer releases decode audio via
  `torchcodec`, which fails here; it breaks both audio loading and augmentation prep.
- **TF needs `TF_FORCE_GPU_ALLOW_GROWTH=true`** or it grabs all 24 GB VRAM and starves cuDNN
  (`CUDNN_STATUS_NOT_INITIALIZED`). Also: don't let torch install CUDA-13 libs alongside TF's
  CUDA-12 set — the mismatch breaks cuDNN. Torch is only used for sample-gen; CPU is fine.
- **AudioSet is no longer downloadable as `.tar` shards**; FMA alone covers background noise.
- Training **must reach step 10000 in one uninterrupted run** — resuming reloads best weights but
  restarts the step counter. Run it under `systemd-run --user` so nothing kills it.

## The big lesson: TTS validation is circular
A cross-validation grid built from Piper TTS (positives, cross-negatives AND test audio all from
the same generator) reported **saturated 255 cross-fires** between phrases. Three training rounds
were spent trying to fix them. On real voice through the device mic, **those cross-fires do not
exist at all** — the models were keying on generator artifacts. Validate on the target hardware
with a real voice before optimizing against a synthetic benchmark.
