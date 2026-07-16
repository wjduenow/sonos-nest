# Wake-word phrases — training + integration spec

Three custom microWakeWord phrases for the sonos-sleep-machine. Each is an independent
model (own .tflite + .json), run in parallel on-device.

| Phrase | Pronunciation | On-device action |
|---|---|---|
| **Kinder Bedtime** | "KIN-der" (German — NOT English "kinder"/KYNE-der) | Play the **sleep track on Sonos** |
| **Kinder Wake-Up** | "KIN-der" | **Stop Sonos** |
| **Kinder Rise and Shine** | "KIN-der" | Play the **wake track** (on-device) |

## Notes
- **"Kinder" = German /ˈkɪndɐ/ ≈ "KIN-der".** English TTS defaults to "KYNE-der" (comparative
  of *kind*). Piper positive-sample generation MUST use a phonetic spelling / phoneme override
  that yields KIN-der. Verify by ear on generated samples BEFORE training (send audio to user).
- Distinct suffixes → low acoustic cross-trigger risk, but still train each with the other two
  phrases as hard negatives; tune probability_cutoff / sliding_window_size per model.
- Actions map onto existing sleep-machine capabilities: Sonos play (sleep track) / Sonos stop /
  local wake-track playback — all already implemented; wiring is Phase 6 (integrate into the app).
