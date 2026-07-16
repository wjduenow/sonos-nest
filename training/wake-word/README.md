# Custom "Kinder …" wake-word training (microWakeWord)

Recipe that produced the three custom wake-word models in
`src/boards/es3c28p/models/kinder_*.tflite`. **Validated on real voice**: each phrase fires only
its own model at 0.95–1.00 on-device, no cross-fires, no false positives.

Framework: [OHF-Voice/micro-wake-word](https://github.com/OHF-Voice/micro-wake-word) (Apache-2.0).
Runs fully **local and account-free**. The workspace (~35 GB of features/datasets) lives OUTSIDE
the repo at `~/mww-train/`; only the scripts live here.

| Phrase | Spelling fed to Piper | Action (see PHRASES.md) |
|---|---|---|
| Kinder Bedtime | `Kinnder Bedtime.` | play sleep track on Sonos |
| Kinder Wake-Up | `Kinnder Wake Up.` | stop Sonos |
| Kinder Rise and Shine | `Kinnder Rise and Shine.` | play the wake track |

## Pipeline
1. `setup_env.sh` — conda env `mww`: TF (GPU) + microWakeWord + Piper + pymicro-features.
2. `gen_positives.sh` — Piper TTS, 2000 samples/phrase, 100 speakers.
3. `aug_prep.py` — augmentation sources → 16 kHz (MIT RIR reverb + FMA background noise).
4. `gen_features_only.py <label>` — augmented spectrogram features (the positives).
5. `gen_clean_xneg.py <label>` — *lightly*-augmented features, used as **clean cross-negatives**.
6. `train_phrase.py <label>` — writes the config + trains 10k steps + exports the streaming tflite.
7. `train_all.sh` — all three, end to end.

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
