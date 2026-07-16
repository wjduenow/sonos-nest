// ES3C28P wake-word bring-up — CUSTOM "Kinder ..." models, 3-up TEST MODE.
//
// Pipeline (proven end-to-end; see the golden-diff note below):
//   ES8311 ADC (16 kHz) -> audio-tools I2S RX (stereo, LEFT slot = mic) -> TFLM audio microfrontend
//   (40-ch log-mel, 30 ms window / 10 ms step, 125-7500 Hz, noise-reduction + PCAN AGC + log)
//   -> int8 quantize (per model's own input scale/zero-point) -> microWakeWord streaming model
//   (int8 [1,3,40], resource-variable state) -> sigmoid -> 5-window average -> fire at cutoff.
//
// All THREE custom models run in parallel off the SAME feature stream (that's how ESPHome does
// multi-wake-word), each with its own arena/interpreter/streaming state. This is a TEST harness:
// it only REPORTS which model fires — no Sonos/audio actions are wired — so the known TTS-grid
// cross-fires (bedtime<-wakeup, wakeup<-rise&shine) can't cause conflicting behaviour while we
// find out what the models actually do with a REAL voice through this mic.
//
// Models: custom-trained locally (Piper TTS positives + cross-phrase hard negatives, 10k steps).
// Feature/quantization constants come from ESPHome's micro_wake_word (preprocessor_settings.h +
// generate_features_()) and MUST match training, or the model runs but never fires.
//
// PROVEN: a golden diff (device PCM -> pymicro-features -> tflite in Python) fires a good model at
// 254/255 through this exact feature+quant+feed path, so this firmware path is known-correct.
#include "wake_test.h"
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

// ---- microWakeWord feature-frontend config (ESPHome preprocessor_settings.h) ----
static constexpr int   kSampleRate     = 16000;
static constexpr int   kFeatureSize    = 40;      // filterbank channels
static constexpr int   kWindowMs       = 30;      // FEATURE_DURATION_MS
static constexpr int   kStepMs         = 10;      // feature_step_size
static constexpr float kLowerBandHz    = 125.0f;
static constexpr float kUpperBandHz    = 7500.0f;

// ---- detection ----
static constexpr int   kSlidingWindow  = 5;       // average this many windows before firing
static constexpr float kProbCutoff     = 0.95f;   // tune per model once we see real-voice scores
static constexpr size_t kArenaSize     = 80 * 1024;   // PSRAM, per model (~19 KB actually used)
static constexpr int   kNumOps         = 20;

// Which model(s) to run. Each inference costs ~17 ms (TFLM reference kernels — we skipped esp-nn),
// and a 3-frame window arrives every 30 ms, so running all THREE (51 ms/30 ms = 170% of real-time)
// overflows the feature queue and feeds the models discontinuous audio. Until esp-nn is vendored,
// test ONE model at a time: set to 0/1/2 to pick, or -1 to run all (expect drops).
static constexpr int   kOnlyModel      = 0;   // 0=Bedtime  1=Wake-Up  2=Rise-and-Shine

// ================= ES8311 mic capture (proven in mic_test.cpp) =================
static DriverDeviceInfo s_pins;
static AudioBoard        s_board(AudioDriverES8311, s_pins);
static I2SCodecStream    s_in(s_board);

static void es8311Write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(I2S_ADDR_ES8311); Wire.write(reg); Wire.write(val); Wire.endTransmission();
}
static uint8_t es8311Read(uint8_t reg) {
  Wire.beginTransmission(I2S_ADDR_ES8311); Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xEE;
  if (Wire.requestFrom((int)I2S_ADDR_ES8311, 1) != 1) return 0xEE;
  return Wire.read();
}
// arduino-audio-driver inits the ES8311 for playback only, leaving the ADC side down. Full esp-adf
// record power-up (REG0E analog + REG01 ADC clock are the load-bearing ones) — see CLAUDE.md.
static void es8311EnableAdc() {
  es8311Write(0x01, 0x3F);                      // enable ADC clock
  es8311Write(0x0A, es8311Read(0x09) & 0x3F);   // ADC serial format, unmuted onto SDOUT
  es8311Write(0x17, 0xF0);                      // ADC digital volume (~+24 dB; 0xFF clipped)
  es8311Write(0x0E, 0x02);                      // power UP analog PGA + ADC modulator
  es8311Write(0x12, 0x00);
  es8311Write(0x14, 0x1A);                      // analog mic (bit6=0, not DMIC) + PGA
  es8311Write(0x0D, 0x01);
  es8311Write(0x15, 0x40);
  es8311Write(0x16, 0x07);                      // mic PGA ~42 dB (low-output mic)
  es8311Write(0x1B, 0x0A); es8311Write(0x1C, 0x6A);
  es8311Write(0x13, 0x10); es8311Write(0x37, 0x48);
  es8311Write(0x45, 0x00); es8311Write(0x44, 0x50);   // ADC on LEFT slot
}

static bool micInit() {
  s_pins.addI2C(PinFunction::CODEC, PIN_I2C_SCL, PIN_I2C_SDA, I2S_ADDR_ES8311, 100000, Wire);
  s_pins.addI2S(PinFunction::CODEC, PIN_I2S_MCLK, PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_DOUT, PIN_I2S_DIN);
  s_pins.begin();
  s_board.begin();
  auto cfg = s_in.defaultConfig(RX_MODE);
  // Stereo: REG44=0x50 puts the mono ADC on the LEFT slot; ESP32 mono RX reads the RIGHT (dead) one.
  cfg.copyFrom(AudioInfo(kSampleRate, 2, 16));
  // Generous DMA buffering so the ~16 ms inferences can't starve I2S (small buffers -> stale audio).
  cfg.buffer_count = 8;
  cfg.buffer_size  = 1024;
  if (!s_in.begin(cfg)) return false;
  s_board.setInputVolume(90);
  es8311EnableAdc();
  return true;
}

// ================= One wake-word model =================
struct WakeModel {
  const char          *name;
  const unsigned char *flatbuffer;
  tflite::MicroInterpreter *interp = nullptr;
  TfLiteTensor *in = nullptr, *out = nullptr;
  float in_scale = 0; int in_zp = 0;
  float out_scale = 0; int out_zp = 0;
  int   stride = 3;          // input dims[1]: feature frames consumed per inference
  int   frameIdx = 0;        // 0..stride-1
  float hist[kSlidingWindow] = {0};
  int   histPos = 0;
  float peak = 0;            // per-report peak of the averaged score
};

static WakeModel s_models[] = {
  {"Kinder Bedtime",        kinder_bedtime_model},
  {"Kinder Wake-Up",        kinder_wakeup_model},
  {"Kinder Rise and Shine", kinder_riseandshine_model},
};
static constexpr int kNumModels = sizeof(s_models) / sizeof(s_models[0]);

static bool modelInit(WakeModel &m) {
  uint8_t *arena = (uint8_t *)heap_caps_malloc(kArenaSize, MALLOC_CAP_SPIRAM);
  if (!arena) return false;
  const tflite::Model *model = tflite::GetModel(m.flatbuffer);
  if (model->version() != TFLITE_SCHEMA_VERSION) return false;

  // microWakeWord streaming graphs use these 20 ops (ESPHome register_streaming_ops_()).
  static tflite::MicroMutableOpResolver<kNumOps> resolver;
  static bool resolverReady = false;
  if (!resolverReady) {
    resolver.AddCallOnce();  resolver.AddVarHandle();     resolver.AddReadVariable();
    resolver.AddAssignVariable(); resolver.AddReshape();  resolver.AddStridedSlice();
    resolver.AddConcatenation(); resolver.AddConv2D();    resolver.AddDepthwiseConv2D();
    resolver.AddMul(); resolver.AddAdd(); resolver.AddMean(); resolver.AddFullyConnected();
    resolver.AddLogistic(); resolver.AddQuantize(); resolver.AddAveragePool2D();
    resolver.AddMaxPool2D(); resolver.AddPad(); resolver.AddPack(); resolver.AddSplitV();
    resolverReady = true;
  }
  // Each model needs its OWN allocator + resource variables (streaming state must not be shared).
  tflite::MicroAllocator *alloc = tflite::MicroAllocator::Create(arena, kArenaSize);
  tflite::MicroResourceVariables *res = tflite::MicroResourceVariables::Create(alloc, 20);
  m.interp = new tflite::MicroInterpreter(model, resolver, alloc, res);
  if (m.interp->AllocateTensors() != kTfLiteOk) return false;
  m.in  = m.interp->input(0);
  m.out = m.interp->output(0);
  m.in_scale = m.in->params.scale;   m.in_zp = m.in->params.zero_point;
  m.out_scale = m.out->params.scale; m.out_zp = m.out->params.zero_point;
  m.stride = m.in->dims->data[1];
  return true;
}

// ================= Capture + frontend task =================
// Dedicated task (ESPHome's design): a slow inference must never starve I2S, or we read stale audio.
static QueueHandle_t s_featQ = nullptr;
static volatile float s_pcmPeakRms = 0;
static volatile int   s_qDrops = 0;

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
  if (!FrontendPopulateState(&cfg, &st, kSampleRate)) {
    Serial.println("[wake] frontend init FAILED"); vTaskDelete(nullptr); return;
  }

  int16_t stereo[640], pcm[320];
  for (;;) {
    size_t got = s_in.readBytes((uint8_t *)stereo, sizeof(stereo));
    size_t n = got / sizeof(int16_t) / 2;
    for (size_t i = 0; i < n; i++) pcm[i] = stereo[2 * i];       // LEFT slot = mic
    { double ss = 0; for (size_t i = 0; i < n; i++) ss += (double)pcm[i] * pcm[i];
      float rms = n ? sqrtf(ss / n) : 0; if (rms > s_pcmPeakRms) s_pcmPeakRms = rms; }

    size_t off = 0;
    while (off < n) {
      size_t used = 0;
      FrontendOutput fo = FrontendProcessSamples(&st, pcm + off, n - off, &used);
      off += used;
      if (used == 0) break;
      if (fo.size == 0) continue;
      // uint16 frontend output -> the "0..26 real" scale (ESPHome generate_features_()).
      // We keep it unquantized here; each model applies its own scale/zero-point below.
      uint16_t frame[kFeatureSize];
      for (size_t i = 0; i < fo.size && i < (size_t)kFeatureSize; i++) frame[i] = fo.values[i];
      if (xQueueSend(s_featQ, frame, 0) != pdTRUE) s_qDrops++;   // never block capture
    }
  }
}

void wakeTestRun() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[wake] CUSTOM Kinder models — 3-up TEST MODE (reports only, no actions)");

  if (!micInit()) { Serial.println("[wake] mic init FAILED"); while (true) delay(1000); }
  for (int i = 0; i < kNumModels; i++) {
    if (!modelInit(s_models[i])) {
      Serial.printf("[wake] model init FAILED: %s\n", s_models[i].name); while (true) delay(1000);
    }
    Serial.printf("[wake] loaded %-22s in[1,%d,%d] scale=%.4f zp=%d  arena=%u  out scale=%.5f\n",
                  s_models[i].name, s_models[i].stride, kFeatureSize, s_models[i].in_scale,
                  s_models[i].in_zp, (unsigned)s_models[i].interp->arena_used_bytes(),
                  s_models[i].out_scale);
  }
  Serial.printf("[wake] internal free=%u  PSRAM free=%u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)ESP.getFreePsram());
  Serial.printf("[wake] fire threshold: avg of %d windows >= %.2f. SAY A PHRASE.\n",
                kSlidingWindow, kProbCutoff);

  s_featQ = xQueueCreate(64, kFeatureSize * sizeof(uint16_t));
  if (!s_featQ) { Serial.println("[wake] queue alloc FAILED"); while (true) delay(1000); }
  xTaskCreatePinnedToCore(captureTask, "wake-cap", 8192, nullptr, 5, nullptr, 0);

  uint16_t frame[kFeatureSize];
  uint32_t infers = 0;
  uint32_t maxInferUs = 0;

  for (;;) {
    if (xQueueReceive(s_featQ, frame, portMAX_DELAY) != pdTRUE) continue;

    for (int i = 0; i < kNumModels; i++) {
      if (kOnlyModel >= 0 && i != kOnlyModel) continue;   // one-at-a-time: keeps us inside real-time
      WakeModel &m = s_models[i];
      // Quantize the shared feature frame with THIS model's own scale/zero-point.
      int8_t *slot = m.in->data.int8 + m.frameIdx * kFeatureSize;
      for (int k = 0; k < kFeatureSize; k++) {
        int32_t v = (int32_t)lroundf((float)frame[k] / 25.6f / m.in_scale) + m.in_zp;
        slot[k] = (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v);
      }
      if (++m.frameIdx < m.stride) continue;
      m.frameIdx = 0;

      uint32_t t0 = micros();
      if (m.interp->Invoke() != kTfLiteOk) { Serial.printf("[wake] %s invoke failed\n", m.name); continue; }
      uint32_t us = micros() - t0; if (us > maxInferUs) maxInferUs = us;

      float prob = (m.out->data.uint8[0] - m.out_zp) * m.out_scale;
      m.hist[m.histPos] = prob; m.histPos = (m.histPos + 1) % kSlidingWindow;
      float avg = 0; for (float p : m.hist) avg += p; avg /= kSlidingWindow;
      if (avg > m.peak) m.peak = avg;

      if (avg >= kProbCutoff) {
        Serial.printf("\n*** FIRED: %-22s (avg=%.2f) ***\n", m.name, avg);
        for (auto &p : m.hist) p = 0;    // debounce
      }
      if (i == (kOnlyModel >= 0 ? kOnlyModel : 0)) infers++;
    }

    // ~2 s heartbeat: per-model peak so you can see near-misses, not just fires.
    if (infers % 64 == 0 && infers) {
      Serial.printf("[wake] peaks  bedtime=%.2f  wakeup=%.2f  rise&shine=%.2f | pcmRms=%.0f infer=%luus drops=%d\n",
                    s_models[0].peak, s_models[1].peak, s_models[2].peak,
                    s_pcmPeakRms, (unsigned long)maxInferUs, s_qDrops);
      for (int i = 0; i < kNumModels; i++) s_models[i].peak = 0;
      s_pcmPeakRms = 0; maxInferUs = 0; infers++;   // avoid re-printing on the same count
    }
  }
}
