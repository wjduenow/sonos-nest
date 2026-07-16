# Wake-word phrases — training + integration spec

Custom microWakeWord phrases for the sonos-sleep-machine. Each is an independent model
(own .tflite + .json), run in parallel on-device.

**Status: integrated and shipped** (commit e1938c5) — this is no longer a forward-looking spec for
Bedtime/Wake-Up. See `plans/03-wake-word-integration.md` for what shipped and why.

| Phrase | Pronunciation | On-device action | Status |
|---|---|---|---|
| **Kinder Bedtime** | "KIN-der" (German — NOT English "kinder"/KYNE-der) | Play the **Sonos Sleep playlist** (the speaker's own library — *not* the SD-stream card) | shipped, fires 0.96–1.00 |
| **Kinder Wake-Up** | "KIN-der" | **Stop Sonos** | shipped, fires 0.96–1.00 |
| **Kinder Rise and Shine** | "KIN-der" | Play the **wake track** (streams from SD to Sonos) | **not shipped** — 0.00 on most real utterances |
| **Kinder Rise** *(planned)* | "KIN-der" | as above — replaces Rise and Shine | to train; needs "Kinder Rise and Shine" as a hard negative (prefix) |

## Notes
- **"Kinder" = German /ˈkɪndɐ/ ≈ "KIN-der".** English TTS defaults to "KYNE-der" (comparative
  of *kind*). Piper positive-sample generation MUST use a phonetic spelling / phoneme override
  that yields KIN-der. Verify by ear on generated samples BEFORE training (send audio to user).
- Distinct suffixes → low acoustic cross-trigger risk, but still train each with the other
  phrases as hard negatives; `cutoff` is per-model in `s_models[]` (`wake_word.cpp`).
- **Short phrases win.** "Bedtime" and "Wake-Up" are reliable; "Rise and Shine" is not, and the
  length/cadence variability is the leading suspect. Favour short and punchy for new phrases.
- Actions map onto existing sleep-machine capabilities (Sonos playlist / Sonos stop / wake-track
  stream), each reusing the same callback as its on-screen button so voice and touch can't drift.
