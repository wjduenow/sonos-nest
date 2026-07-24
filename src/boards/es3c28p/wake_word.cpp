// ES3C28P on-device wake word — implements the board HAL's wakeWord* (core/board.h).
//
// Pipeline (proven; the bring-up lives in wake_test.cpp / the `sleep-machine-wake` env):
//   ES8311 ADC (16 kHz) -> audio-tools I2S RX (stereo, LEFT slot = mic) -> TFLM audio microfrontend
//   (40-ch log-mel, 30 ms window / 10 ms step, 125-7500 Hz, noise-reduction + PCAN AGC + log)
//   -> int8 quantize (per model's own scale/zero-point) -> microWakeWord streaming model
//   (int8 [1,3,40], resource-variable state) -> sigmoid -> 5-window average -> fire at cutoff.
//
// Three custom models ("Kinder Bedtime / Wake-Up / Rise and Shine", trained per training/wake-word/)
// run in parallel off one feature stream, each ~4 ms/inference with esp-nn — ~40% of real-time.
//
// This module ONLY reports which phrase was heard. What a phrase does is the unit's business
// (see units/sleep_machine/screens.cpp) — boards must not reach into g_pending/settings.
//
// Two tasks, deliberately: capture+frontend must never be blocked by inference or I2S goes stale
// and the models see discontinuous audio (this cost real debugging time — see CLAUDE.md).
#include "core/board.h"
#include "pins.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "esp_heap_caps.h"

#include "AudioTools.h"
#include "AudioTools/AudioLibs/I2SCodecStream.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"

#include "models/kinder_bedtime_model.h"
#include "models/kinder_wakeup_model.h"
#include "models/kinder_riseandshine_model.h"

// Feature frontend — MUST match how the models were trained (ESPHome preprocessor_settings.h).
static constexpr int    kSampleRate    = 16000;
static constexpr int    kFeatureSize   = 40;
static constexpr int    kWindowMs      = 30;
static constexpr int    kStepMs        = 10;
static constexpr float  kLowerBandHz   = 125.0f;
static constexpr float  kUpperBandHz   = 7500.0f;
// Detection: average this many windows and fire above the model's cutoff. 0.95 is the default,
// measured clean on real voice (fires 0.96-1.00, silent on the other phrases); each model can
// override it (see s_models). Raise if daily use false-triggers.
static constexpr int    kSlidingWindow = 5;
static constexpr float  kProbCutoff    = 0.95f;
// PSRAM. 80 KB is the size the bring-up proved; arena_used_bytes reports ~34 KB but the streaming
// resource-variables allocate more from the same arena, so don't trim this to the reported figure
// (48 KB silently corrupted the tensor metadata rather than failing AllocateTensors).
static constexpr size_t kArenaSize     = 80 * 1024;
static constexpr int    kNumOps        = 20;
// Ignore repeats within this window — one utterance produces several consecutive fires.
static constexpr uint32_t kDebounceMs  = 1500;

static DriverDeviceInfo s_pins;
static AudioBoard       s_board(AudioDriverES8311, s_pins);
static I2SCodecStream   s_in(s_board);

static void es8311Write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(I2S_ADDR_ES8311); Wire.write(reg); Wire.write(val); Wire.endTransmission();
}
static uint8_t es8311Read(uint8_t reg) {
  Wire.beginTransmission(I2S_ADDR_ES8311); Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xEE;
  if (Wire.requestFrom((int)I2S_ADDR_ES8311, 1) != 1) return 0xEE;
  return Wire.read();
}
// arduino-audio-driver only inits the ES8311 for playback; the ADC side stays powered down.
// Full esp-adf record power-up — REG0E (analog) + REG01 (ADC clock) are the load-bearing ones.
static void es8311EnableAdc() {
  es8311Write(0x01, 0x3F);
  es8311Write(0x0A, es8311Read(0x09) & 0x3F);   // ADC format, unmuted onto SDOUT
  es8311Write(0x17, 0xF0);                      // ADC digital volume (~+24 dB; 0xFF clips)
  es8311Write(0x0E, 0x02);                      // power up analog PGA + ADC modulator
  es8311Write(0x12, 0x00);
  es8311Write(0x14, 0x1A);                      // analog mic + PGA
  es8311Write(0x0D, 0x01);
  es8311Write(0x15, 0x40);
  es8311Write(0x16, 0x07);                      // mic PGA ~42 dB (low-output mic)
  es8311Write(0x1B, 0x0A); es8311Write(0x1C, 0x6A);
  es8311Write(0x13, 0x10); es8311Write(0x37, 0x48);
  es8311Write(0x45, 0x00); es8311Write(0x44, 0x50);   // ADC on LEFT slot
}

struct WakeModel {
  const char          *name;
  const unsigned char *flatbuffer;
  float                cutoff;      // per-model; see kProbCutoff
  tflite::MicroInterpreter *interp = nullptr;
  TfLiteTensor *in = nullptr, *out = nullptr;
  float in_scale = 0;  int in_zp = 0;
  float out_scale = 0; int out_zp = 0;
  int   stride = 3, frameIdx = 0;
  float hist[kSlidingWindow] = {0};
  int   histPos = 0;
  uint32_t lastFireMs = 0;
};

static WakeModel s_models[] = {
  {"Kinder Bedtime", kinder_bedtime_model, kProbCutoff},
  {"Kinder Wake-Up", kinder_wakeup_model,  kProbCutoff},
  // "Kinder Rise and Shine" is BUILT AND VENDORED (models/kinder_riseandshine_model.h) but NOT
  // shipped: on real voice it scores 0.00 on most utterances and 0.75-0.99 on a few, where these
  // two sit at 0.96-1.00 every time. Lowering its cutoff to 0.70 did not help — the failures are
  // zeros, not near-misses, so there is nothing to lower the bar to. Suspected cause: the phrase is
  // long and its cadence varies, which suits this architecture (3-frame streaming windows) poorly.
  // A shorter "Kinder Rise" is the planned replacement. Re-add here once a model clears 0.95
  // repeatably, and add the matching branch in handleWakeWord(). Running it costs ~228 B internal
  // + ~4 ms/inference. See plans/03-wake-word-integration.md.
  // {"Kinder Rise and Shine", kinder_riseandshine_model, 0.70f},
};
static constexpr int kModelCount = sizeof(s_models) / sizeof(s_models[0]);

static QueueHandle_t   s_featQ   = nullptr;   // capture task -> inference task (one 40-ch frame)
static volatile int    s_detected = -1;       // last phrase heard; drained by wakeWordPoll()
static bool            s_running  = false;

// Set to 1 to print a 2 s heartbeat: mic level + each model's peak score since the last beat.
// This is THE tool for tuning a new phrase — it separates "model scored 0.7, raise/lower the
// cutoff" from "model scored 0.00, it's not recognising you at all", and it shows rms so a
// negative result can't be blamed on a too-quiet (or clipping, >~25000) test. Leave it off in
// normal builds: it prints from the inference hot path.
#define WAKE_DEBUG 0
#if WAKE_DEBUG
static float             s_peak[sizeof(s_models) / sizeof(s_models[0])] = {0};
static volatile uint32_t s_pcmRms = 0;
#endif

// Returns true only on FULL success, and leaves m.interp==nullptr on any failure with the arena
// freed — so wakeBringUp() can safely retry a model that didn't come up without leaking or
// double-initialising one that did.
static bool modelInit(WakeModel &m) {
  uint8_t *arena = (uint8_t *)heap_caps_malloc(kArenaSize, MALLOC_CAP_SPIRAM);
  if (!arena) return false;
  const tflite::Model *model = tflite::GetModel(m.flatbuffer);
  if (model->version() != TFLITE_SCHEMA_VERSION) { heap_caps_free(arena); return false; }

  // The 20 ops a microWakeWord streaming graph uses (ESPHome register_streaming_ops_()).
  static tflite::MicroMutableOpResolver<kNumOps> resolver;
  static bool resolverReady = false;
  if (!resolverReady) {
    resolver.AddCallOnce();       resolver.AddVarHandle();   resolver.AddReadVariable();
    resolver.AddAssignVariable(); resolver.AddReshape();     resolver.AddStridedSlice();
    resolver.AddConcatenation();  resolver.AddConv2D();      resolver.AddDepthwiseConv2D();
    resolver.AddMul();            resolver.AddAdd();         resolver.AddMean();
    resolver.AddFullyConnected(); resolver.AddLogistic();    resolver.AddQuantize();
    resolver.AddAveragePool2D();  resolver.AddMaxPool2D();   resolver.AddPad();
    resolver.AddPack();           resolver.AddSplitV();
    resolverReady = true;
  }
  // Each model needs its OWN allocator + resource variables — the streaming state must not be shared.
  tflite::MicroAllocator *alloc = tflite::MicroAllocator::Create(arena, kArenaSize);
  tflite::MicroResourceVariables *res = tflite::MicroResourceVariables::Create(alloc, 20);
  tflite::MicroInterpreter *interp = new tflite::MicroInterpreter(model, resolver, alloc, res);
  if (interp->AllocateTensors() != kTfLiteOk) { delete interp; heap_caps_free(arena); return false; }
  m.interp = interp;
  m.in = m.interp->input(0); m.out = m.interp->output(0);
  m.in_scale  = m.in->params.scale;   m.in_zp  = m.in->params.zero_point;
  m.out_scale = m.out->params.scale;  m.out_zp = m.out->params.zero_point;
  m.stride    = m.in->dims->data[1];
  return true;
}

// Capture + frontend. Kept off the inference task on purpose: if this blocks, I2S overflows and
// the models are fed stale/discontinuous audio (they then never fire).
static void captureTask(void *) {
  static FrontendConfig cfg;
  FrontendFillConfigWithDefaults(&cfg);
  cfg.window.size_ms                       = kWindowMs;
  cfg.window.step_size_ms                  = kStepMs;
  cfg.filterbank.num_channels              = kFeatureSize;
  cfg.filterbank.lower_band_limit          = kLowerBandHz;
  cfg.filterbank.upper_band_limit          = kUpperBandHz;
  cfg.noise_reduction.smoothing_bits       = 10;
  cfg.noise_reduction.even_smoothing       = 0.025f;
  cfg.noise_reduction.odd_smoothing        = 0.06f;
  cfg.noise_reduction.min_signal_remaining = 0.05f;
  cfg.pcan_gain_control.enable_pcan        = 1;
  cfg.pcan_gain_control.strength           = 0.95f;
  cfg.pcan_gain_control.offset             = 80.0f;
  cfg.pcan_gain_control.gain_bits          = 21;
  cfg.log_scale.enable_log                 = 1;
  cfg.log_scale.scale_shift                = 6;
  static FrontendState st;
  if (!FrontendPopulateState(&cfg, &st, kSampleRate)) { vTaskDelete(nullptr); return; }

  int16_t stereo[640], pcm[320];
  for (;;) {
    size_t got = s_in.readBytes((uint8_t *)stereo, sizeof(stereo));
    size_t n = got / sizeof(int16_t) / 2;
    for (size_t i = 0; i < n; i++) pcm[i] = stereo[2 * i];      // LEFT slot = mic
#if WAKE_DEBUG
    uint64_t sq = 0;                                            // mic level for the heartbeat
    for (size_t i = 0; i < n; i++) sq += (int32_t)pcm[i] * pcm[i];
    if (n) s_pcmRms = (uint32_t)sqrt((double)sq / n);
#endif
    size_t off = 0;
    while (off < n) {
      size_t used = 0;
      FrontendOutput fo = FrontendProcessSamples(&st, pcm + off, n - off, &used);
      off += used;
      if (used == 0) break;
      if (fo.size == 0) continue;
      uint16_t frame[kFeatureSize];
      for (size_t i = 0; i < fo.size && i < (size_t)kFeatureSize; i++) frame[i] = fo.values[i];
      xQueueSend(s_featQ, frame, 0);      // drop rather than block capture
    }
  }
}

// Inference: all models share the feature stream, each keeps its own 3-frame window + state.
static void inferTask(void *) {
  uint16_t frame[kFeatureSize];
  for (;;) {
    if (xQueueReceive(s_featQ, frame, portMAX_DELAY) != pdTRUE) continue;
    for (int i = 0; i < kModelCount; i++) {
      WakeModel &m = s_models[i];
      int8_t *slot = m.in->data.int8 + m.frameIdx * kFeatureSize;
      for (int k = 0; k < kFeatureSize; k++) {
        // frontend uint16 -> the "0..26 real" scale -> this model's int8 quantization
        int32_t v = (int32_t)lroundf((float)frame[k] / 25.6f / m.in_scale) + m.in_zp;
        slot[k] = (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v);
      }
      if (++m.frameIdx < m.stride) continue;
      m.frameIdx = 0;
      if (m.interp->Invoke() != kTfLiteOk) continue;

      float prob = (m.out->data.uint8[0] - m.out_zp) * m.out_scale;
      m.hist[m.histPos] = prob; m.histPos = (m.histPos + 1) % kSlidingWindow;
      float avg = 0; for (float p : m.hist) avg += p; avg /= kSlidingWindow;
#if WAKE_DEBUG
      if (avg > s_peak[i]) s_peak[i] = avg;
      static uint32_t lastHb = 0;
      if (millis() - lastHb > 2000) {
        lastHb = millis();
        Serial.printf("[wake] hb rms=%u peak:", (unsigned)s_pcmRms);
        for (int k = 0; k < kModelCount; k++) { Serial.printf(" %.2f", s_peak[k]); s_peak[k] = 0; }
        Serial.println();
      }
#endif
      if (avg < m.cutoff) continue;

      uint32_t now = millis();
      if (now - m.lastFireMs < kDebounceMs) continue;   // one utterance -> several fires
      m.lastFireMs = now;
      for (auto &p : m.hist) p = 0;
      s_detected = i;
      Serial.printf("[wake] heard \"%s\" (%.2f)\n", m.name, avg);
    }
  }
}

// Idempotent bring-up: each step guards on what it already did, so this can be re-run after a
// partial failure (see wakeRetryTask) without re-opening the mic, rebuilding a model, or spawning a
// duplicate task. Returns true only once the whole pipeline — mic + every model + both tasks — is up.
// The realistic failure point is internal SRAM: s_in.begin() (I2S DMA) and the two task stacks all
// draw from it, and a boot-time fragmentation dip can starve them. modelInit() draws from PSRAM.
static bool s_micReady = false;
static bool wakeBringUp() {
  if (!s_micReady) {
    s_pins.addI2C(PinFunction::CODEC, PIN_I2C_SCL, PIN_I2C_SDA, I2S_ADDR_ES8311, 100000, Wire);
    s_pins.addI2S(PinFunction::CODEC, PIN_I2S_MCLK, PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_DOUT, PIN_I2S_DIN);
    s_pins.begin();
    s_board.begin();
    auto cfg = s_in.defaultConfig(RX_MODE);
    // Stereo: REG44=0x50 routes the mono ADC to the LEFT slot; mono RX would read the dead RIGHT one.
    cfg.copyFrom(AudioInfo(kSampleRate, 2, 16));
    cfg.buffer_count = 4;      // absorb inference jitter; small buffers -> stale audio
    cfg.buffer_size  = 512;
    if (!s_in.begin(cfg)) { Serial.println("[wake] mic init failed"); return false; }
    s_board.setInputVolume(90);
    es8311EnableAdc();
    s_micReady = true;
  }

  for (int i = 0; i < kModelCount; i++) {
    if (s_models[i].interp) continue;                  // already built on an earlier attempt
    if (!modelInit(s_models[i])) {
      Serial.printf("[wake] model init failed: %s\n", s_models[i].name);
      return false;
    }
  }

  if (!s_featQ) {
    s_featQ = xQueueCreate(64, kFeatureSize * sizeof(uint16_t));
    if (!s_featQ) return false;
  }
  // BOTH pinned to core 1, on purpose: core 0 belongs to the network (netTask prio 2, media-httpd
  // prio 1). These must outrank whatever shares their core — capture especially, or I2S overflows
  // and the models see stale audio — so putting them on core 0 starves Sonos instead. Ordering
  // here is capture > inference > uiTask (prio 3): dropping audio breaks detection, a jittery
  // repaint doesn't. Stacks are sized off measured high-water marks (~3.5 KB / ~2.5 KB peak) plus
  // headroom; internal SRAM is the scarce resource and overshooting starves LWIP of socket buffers.
  static TaskHandle_t hCap = nullptr, hInf = nullptr;
  if (!hCap && xTaskCreatePinnedToCore(captureTask, "wake-cap", 5120, nullptr, 5, &hCap, 1) != pdPASS) {
    hCap = nullptr; return false;                      // out of internal SRAM for the stack — retry later
  }
  if (!hInf && xTaskCreatePinnedToCore(inferTask, "wake-inf", 4096, nullptr, 4, &hInf, 1) != pdPASS) {
    hInf = nullptr; return false;
  }
  return true;
}

// A failed init used to disable voice for the whole session, silently. Boot-time heap fragmentation
// (Sonos discovery + the media httpd starting up) can transiently starve the ~31 KB of internal SRAM
// the engine needs; a few seconds later it recovers. Retry a handful of times, spaced out, and log
// the internal-free each attempt so a *persistent* shortfall shows up in the serial log instead of
// looking like a mystery. Low prio, core 1 — it mostly sleeps.
static void wakeRetryTask(void *) {
  for (int attempt = 1; attempt <= 6 && !s_running; attempt++) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    Serial.printf("[wake] retry %d (internal free=%u)\n", attempt,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    if (wakeBringUp()) {
      s_running = true;
      Serial.printf("[wake] listening after retry %d for %d phrases (internal free=%u)\n",
                    attempt, kModelCount, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }
  }
  if (!s_running) Serial.println("[wake] giving up after retries — voice disabled this session");
  vTaskDelete(nullptr);
}

bool wakeWordInit() {
  if (s_running) return true;
  if (wakeBringUp()) {
    s_running = true;
    Serial.printf("[wake] listening for %d phrases (internal free=%u)\n",
                  kModelCount, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return true;
  }
  // Don't give up — schedule background retries so a transient boot-time SRAM dip can't kill voice
  // for the session. Log the free figure now so the shortfall is on the record from the first try.
  Serial.printf("[wake] init incomplete (internal free=%u) — retrying in background\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  xTaskCreatePinnedToCore(wakeRetryTask, "wake-retry", 3072, nullptr, 1, nullptr, 1);
  return false;
}

int wakeWordPoll() {
  int d = s_detected;
  if (d >= 0) s_detected = -1;
  return d;
}

const char *wakeWordPhrase(int i) {
  return (i >= 0 && i < kModelCount) ? s_models[i].name : nullptr;
}

int wakeWordCount() { return kModelCount; }
