# Vendored TensorFlow Lite Micro (`lib/tflm`)

This is a **vendored subset of Espressif's [esp-tflite-micro](https://github.com/espressif/esp-tflite-micro)**
(current TensorFlow Lite Micro for ESP chips, the same TFLM ESPHome runs), packaged to compile as a
PlatformIO library under the **Arduino** framework (arduino-esp32 2.0.17 / IDF 4.4). Used by the
`sleep-machine-wake` env for on-device microWakeWord wake-word detection.

License: **Apache-2.0** (upstream). See upstream `LICENSE`.

## Why vendored (not a lib_dep)
`esp-tflite-micro` ships as an ESP-IDF component with a `CMakeLists.txt` build; there is no
PlatformIO-registry package and no Arduino-framework path. Beginner wrappers (EdgeNeuron) hide the
op resolver, so they can't drive microWakeWord's **streaming** models (which need the resource-
variable ops `VarHandle`/`ReadVariable`/`AssignVariable` + a custom 20-op `MicroMutableOpResolver`).
So the source tree is vendored and built via `library.json` (`build.srcFilter` + flags).

## What was changed vs upstream
- **Reference kernels only.** The `esp_nn` optimized kernels (`tensorflow/lite/micro/kernels/esp_nn/`)
  were **deleted** to avoid pulling in the separate `esp-nn` dependency. Inference is ~16 ms this way
  (fine; < 30 ms budget). Re-add esp-nn later for ~2× speed if needed.
- **Classic external microfrontend added.** `tensorflow/lite/experimental/microfrontend/lib/` is
  copied from the older TFLM Arduino port (tanakamasayuki) — the standalone `FrontendConfig` /
  `FrontendProcessSamples` C API microWakeWord's *external* feature generation uses. Upstream
  esp-tflite-micro moved feature ops into `signal/` (graph ops), which we don't use for features.
- `build.flags` carry the include dirs (`third_party/{flatbuffers/include,gemmlowp,ruy,kissfft}`),
  `-DTF_LITE_STATIC_MEMORY -DTF_LITE_DISABLE_X86_NEON -fno-rtti -fno-exceptions` (TFLM makes
  `operator delete` private → placement-new only compiles with exceptions off), and `-w`.
- `srcFilter` excludes `tensorflow/lite/micro/micro_time.cc` (the `esp/micro_time.cc` variant is used).

## Status
The pipeline is **proven correct** — a clean Piper-TTS "okay nabu" fires `okay_nabu.tflite` at
254/255 through this exact TFLM + microfrontend + quantization path (validated off-device with
`pymicro-features` + `tflite-runtime`). Real-voice detection through the ES8311 mic is weak
(~5-20/255), a model/acoustic-matching gap, not a firmware bug — fix with a better-matched or
custom-trained model. See `src/boards/es3c28p/wake_test.cpp`.

## Re-vendoring
`git clone --depth 1 https://github.com/espressif/esp-tflite-micro`, copy `tensorflow/`, `signal/`,
`third_party/`; `rm -rf tensorflow/lite/micro/kernels/esp_nn`; copy the classic microfrontend into
`tensorflow/lite/experimental/microfrontend/lib/`; keep this `library.json`.
