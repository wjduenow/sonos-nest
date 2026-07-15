// ES3C28P wake-word bring-up — Phase 1: mic -> microfrontend -> microWakeWord streaming model.
//
// Builds on the two proven pieces: the ES8311 mic capture (mic_test.cpp) and the TFLM model load
// (this file's step 1). It wires the full detection pipeline:
//
//   ES8311 ADC (16 kHz mono) -> audio-tools I2S RX -> TFLM audio microfrontend (40-ch log-mel,
//   30 ms window / 10 ms step, 125-7500 Hz, noise-reduction + PCAN AGC + log) -> uint16->int8
//   quantize -> streaming model (input int8 [1,3,40], resource-variable state) -> sigmoid prob
//   -> 5-window average -> fire when >= probability_cutoff.
//
// Every constant below that touches the feature frontend or the int8 quantization is copied from
// ESPHome's micro_wake_word (preprocessor_settings.h + generate_features_()) and the model's JSON
// manifest. They MUST match how the model was trained — a wrong window size or quantization divisor
// yields a model that runs but never fires (the same silent-misconfig failure the mic had).
//
// Model: esphome/micro-wake-word-models v2 okay_nabu (Apache-2.0), embedded in models/.
// Built only by the `sleep-machine-wake` env.
//
// STATUS (Phase 1): the pipeline is PROVEN CORRECT end-to-end. A golden comparison (device PCM ->
// pymicro-features golden features -> okay_nabu.tflite in Python) fires the model at 254/255 on a
// clean Piper-TTS "okay nabu" using this exact feature+quantization+feed path. What remains is a
// real-voice/acoustic gap: live speech through this mic peaks at only ~5-20/255 (threshold 247),
// because microWakeWord's stock models are trained on TTS and are picky about real voices + mic
// frequency response. The fix is a better-matched model (custom-trained on this voice+mic, or a
// higher-data stock word), NOT firmware changes. kCaptureMode below dumps PCM+features for the
// golden diff; see the off-device tooling used to prove this (pymicro-features + tflite-runtime).
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

#include "models/okay_nabu_model.h"

// ---- microWakeWord feature-frontend config (ESPHome preprocessor_settings.h) ----
static constexpr int   kSampleRate      = 16000;
static constexpr int   kFeatureSize     = 40;     // PREPROCESSOR_FEATURE_SIZE (filterbank channels)
static constexpr int   kWindowMs        = 30;     // FEATURE_DURATION_MS
static constexpr int   kStepMs          = 10;     // manifest feature_step_size
static constexpr float kLowerBandHz     = 125.0f;
static constexpr float kUpperBandHz     = 7500.0f;

// ---- streaming model window + detection (okay_nabu manifest) ----
static constexpr int   kFramesPerInfer  = 3;      // model input is [1, 3, 40]
static constexpr int   kSlidingWindow   = 5;      // average this many probabilities before firing
static constexpr float kProbCutoff      = 0.97f;  // manifest probability_cutoff

static constexpr size_t kArenaSize      = 80 * 1024;   // PSRAM; ~19 KB actually used (measured)
static constexpr int    kNumOps         = 20;

// Golden-feature capture mode: record a clip, dump raw PCM + the int8 features THIS frontend
// produces from it, then halt. Off-device we regenerate golden features (pymicro-features) from
// the same PCM and diff — isolates a frontend mismatch from a deeper model/feed issue. Flip to
// false for normal live detection.
static constexpr bool   kCaptureMode    = false;
static constexpr size_t kCapSamples     = 48000;   // 3 s at 16 kHz

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
// Full esp-adf ADC power-up: arduino-audio-driver inits for playback only, leaving the analog ADC
// powered down. See mic_test.cpp / the CLAUDE.md gotcha for the full rationale.
static void es8311EnableAdc() {
  es8311Write(0x01, 0x3F);                 // enable ADC clock
  es8311Write(0x0A, es8311Read(0x09) & 0x3F);   // mirror DAC format, unmute ADC onto SDOUT
  es8311Write(0x17, 0xD0);                 // ADC digital volume ~+8 dB
  es8311Write(0x0E, 0x02);                 // power up analog PGA + ADC modulator
  es8311Write(0x12, 0x00);
  es8311Write(0x14, 0x1A);                 // analog mic + PGA gain
  es8311Write(0x0D, 0x01);
  es8311Write(0x15, 0x40);
  es8311Write(0x17, 0xF0);                 // ADC digital volume ~+24 dB — target a clean ~3000 rms
  es8311Write(0x16, 0x07);                 // mic PGA ~42 dB analog
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
  // Capture STEREO: REG44=0x50 routes the ES8311's single ADC to the LEFT I2S slot, and ESP32
  // mono RX reads the RIGHT slot (dead) — so we take 2 channels and use the left, as mic_test did.
  cfg.copyFrom(AudioInfo(kSampleRate, 2, 16));
  // Generous DMA buffering: the ~16 ms inference blocks this loop, and with small DMA buffers the
  // I2S RX overflows in that gap and returns stale chunks (mic looks dead). ~160 ms of buffering
  // absorbs the gap so we always read live audio; detection latency grows slightly, which is fine.
  cfg.buffer_count = 8;
  cfg.buffer_size  = 1024;
  if (!s_in.begin(cfg)) return false;
  s_board.setInputVolume(90);                    // match mic_test's proven init
  es8311EnableAdc();
  return true;
}

// ================= TFLM streaming model =================
static tflite::MicroInterpreter *s_interp = nullptr;
static TfLiteTensor *s_input = nullptr, *s_output = nullptr;

static bool modelInit() {
  uint8_t *arena = (uint8_t *)heap_caps_malloc(kArenaSize, MALLOC_CAP_SPIRAM);
  if (!arena) return false;

  const tflite::Model *model = tflite::GetModel(okay_nabu_model);
  if (model->version() != TFLITE_SCHEMA_VERSION) return false;

  static tflite::MicroMutableOpResolver<kNumOps> resolver;
  resolver.AddCallOnce();  resolver.AddVarHandle();     resolver.AddReadVariable();
  resolver.AddAssignVariable(); resolver.AddReshape();  resolver.AddStridedSlice();
  resolver.AddConcatenation(); resolver.AddConv2D();    resolver.AddDepthwiseConv2D();
  resolver.AddMul(); resolver.AddAdd(); resolver.AddMean(); resolver.AddFullyConnected();
  resolver.AddLogistic(); resolver.AddQuantize(); resolver.AddAveragePool2D();
  resolver.AddMaxPool2D(); resolver.AddPad(); resolver.AddPack(); resolver.AddSplitV();

  tflite::MicroAllocator *allocator = tflite::MicroAllocator::Create(arena, kArenaSize);
  tflite::MicroResourceVariables *resources = tflite::MicroResourceVariables::Create(allocator, 20);
  static tflite::MicroInterpreter interp(model, resolver, allocator, resources);
  if (interp.AllocateTensors() != kTfLiteOk) return false;
  s_interp = &interp;
  s_input  = interp.input(0);
  s_output = interp.output(0);
  return true;
}

// ================= Golden-feature capture =================
static void configFrontend(FrontendConfig &c) {
  FrontendFillConfigWithDefaults(&c);
  c.window.size_ms = kWindowMs;                 c.window.step_size_ms = kStepMs;
  c.filterbank.num_channels = kFeatureSize;     c.filterbank.lower_band_limit = kLowerBandHz;
  c.filterbank.upper_band_limit = kUpperBandHz;
  c.noise_reduction.smoothing_bits = 10;        c.noise_reduction.even_smoothing = 0.025f;
  c.noise_reduction.odd_smoothing = 0.06f;      c.noise_reduction.min_signal_remaining = 0.05f;
  c.pcan_gain_control.enable_pcan = 1;          c.pcan_gain_control.strength = 0.95f;
  c.pcan_gain_control.offset = 80.0f;           c.pcan_gain_control.gain_bits = 21;
  c.log_scale.enable_log = 1;                   c.log_scale.scale_shift = 6;
}

static void b64dump(const uint8_t *data, size_t len) {
  static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char line[81]; int col = 0;
  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = data[i] << 16;
    if (i + 1 < len) n |= data[i + 1] << 8;
    if (i + 2 < len) n |= data[i + 2];
    line[col++] = T[(n >> 18) & 63];
    line[col++] = T[(n >> 12) & 63];
    line[col++] = (i + 1 < len) ? T[(n >> 6) & 63] : '=';
    line[col++] = (i + 2 < len) ? T[n & 63] : '=';
    if (col >= 76) { line[col] = 0; Serial.println(line); col = 0; }
  }
  if (col) { line[col] = 0; Serial.println(line); }
}

// Record kCapSamples of mic PCM, dump it (base64) plus the int8 features our frontend derives from
// exactly that PCM, then halt. Deterministic: same audio in, same features out — perfect for an
// off-device golden diff.
static void captureAndDump() {
  int16_t *pcm = (int16_t *)heap_caps_malloc(kCapSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  if (!pcm) { Serial.println("[cap] PCM buffer alloc FAILED"); for (;;) delay(1000); }

  Serial.println("[cap] recording 3 s in 1 s… SAY \"OKAY NABU\" NOW");
  delay(1000);
  int16_t stereo[640];
  size_t got = 0;
  while (got < kCapSamples) {
    size_t n = s_in.readBytes((uint8_t *)stereo, sizeof(stereo)) / sizeof(int16_t) / 2;
    for (size_t i = 0; i < n && got < kCapSamples; i++) pcm[got++] = stereo[2 * i];
  }
  Serial.println("[cap] done recording");

  Serial.printf("[cap] BEGIN_PCM samples=%u rate=16000\n", (unsigned)kCapSamples);
  b64dump((const uint8_t *)pcm, kCapSamples * sizeof(int16_t));
  Serial.println("[cap] END_PCM");

  static FrontendConfig cfg; configFrontend(cfg);
  static FrontendState st;
  FrontendPopulateState(&cfg, &st, kSampleRate);
  Serial.println("[cap] BEGIN_FEAT cols=40");
  size_t off = 0;
  while (off < kCapSamples) {
    size_t consumed = 0;
    FrontendOutput fo = FrontendProcessSamples(&st, pcm + off, kCapSamples - off, &consumed);
    off += consumed;
    if (consumed == 0) break;
    if (fo.size == 0) continue;
    char row[8 * 40]; int c = 0;
    for (size_t i = 0; i < fo.size; i++) {
      int32_t v = ((int32_t)fo.values[i] * 256 + 333) / 666; v += INT8_MIN;
      int8_t q = (int8_t)(v < INT8_MIN ? INT8_MIN : v > INT8_MAX ? INT8_MAX : v);
      c += snprintf(row + c, sizeof(row) - c, "%d%s", q, i + 1 < fo.size ? "," : "");
    }
    Serial.println(row);
  }
  Serial.println("[cap] END_FEAT");
  Serial.println("[cap] DONE — reset to re-capture");
  for (;;) delay(1000);
}

// ================= Decoupled capture + frontend task =================
// ESPHome's architecture: a dedicated task drains I2S and generates features continuously into a
// queue, so a slow inference can never starve capture (a coupled single loop drops audio in the
// ~16 ms inference gap). Each queue item is one 40-channel int8 feature frame.
static QueueHandle_t s_featQ = nullptr;
static volatile float s_pcmPeakRms = 0;    // diagnostics (written by capture task)
static volatile int   s_qDrops = 0;        // frames dropped because the queue was full

static void captureTask(void *) {
  static FrontendConfig fe_cfg;
  FrontendFillConfigWithDefaults(&fe_cfg);
  fe_cfg.window.size_ms                       = kWindowMs;
  fe_cfg.window.step_size_ms                  = kStepMs;
  fe_cfg.filterbank.num_channels              = kFeatureSize;
  fe_cfg.filterbank.lower_band_limit          = kLowerBandHz;
  fe_cfg.filterbank.upper_band_limit          = kUpperBandHz;
  fe_cfg.noise_reduction.smoothing_bits       = 10;
  fe_cfg.noise_reduction.even_smoothing       = 0.025f;
  fe_cfg.noise_reduction.odd_smoothing        = 0.06f;
  fe_cfg.noise_reduction.min_signal_remaining = 0.05f;
  fe_cfg.pcan_gain_control.enable_pcan        = 1;
  fe_cfg.pcan_gain_control.strength           = 0.95f;
  fe_cfg.pcan_gain_control.offset             = 80.0f;
  fe_cfg.pcan_gain_control.gain_bits          = 21;
  fe_cfg.log_scale.enable_log                 = 1;
  fe_cfg.log_scale.scale_shift                = 6;
  static FrontendState fe_state;
  if (!FrontendPopulateState(&fe_cfg, &fe_state, kSampleRate)) {
    Serial.println("[wake] frontend init FAILED"); vTaskDelete(nullptr); return;
  }

  int16_t stereo[640];              // 20 ms of L/R interleaved
  int16_t pcm[320];                 // extracted LEFT channel (the mic)
  for (;;) {
    size_t got = s_in.readBytes((uint8_t *)stereo, sizeof(stereo));
    size_t nsamp = got / sizeof(int16_t) / 2;
    for (size_t i = 0; i < nsamp; i++) pcm[i] = stereo[2 * i];
    { double ss = 0; for (size_t i = 0; i < nsamp; i++) ss += (double)pcm[i] * pcm[i];
      float rms = nsamp ? sqrtf(ss / nsamp) : 0; if (rms > s_pcmPeakRms) s_pcmPeakRms = rms; }

    size_t off = 0;
    while (off < nsamp) {
      size_t consumed = 0;
      FrontendOutput fo = FrontendProcessSamples(&fe_state, pcm + off, nsamp - off, &consumed);
      off += consumed;
      if (consumed == 0) break;
      if (fo.size == 0) continue;
      int8_t frame[kFeatureSize];
      for (size_t i = 0; i < fo.size && i < (size_t)kFeatureSize; i++) {
        int32_t v = ((int32_t)fo.values[i] * 256 + 333) / 666;
        v += INT8_MIN;
        frame[i] = (int8_t)(v < INT8_MIN ? INT8_MIN : v > INT8_MAX ? INT8_MAX : v);
      }
      if (xQueueSend(s_featQ, frame, 0) != pdTRUE) s_qDrops++;   // never block capture
    }
  }
}

void wakeTestRun() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[wake] microWakeWord detection bring-up — say \"okay nabu\"");

  if (!micInit())   { Serial.println("[wake] mic init FAILED");   while (true) delay(1000); }
  if (kCaptureMode) captureAndDump();   // records + dumps PCM & features, then halts
  if (!modelInit()) { Serial.println("[wake] model init FAILED"); while (true) delay(1000); }
  Serial.printf("[wake] ready. input int8 [%d,%d,%d]  arena=%u  internal free=%u\n",
                s_input->dims->data[0], s_input->dims->data[1], s_input->dims->data[2],
                (unsigned)s_interp->arena_used_bytes(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  const float out_scale = s_output->params.scale;
  const int   out_zp    = s_output->params.zero_point;
  Serial.printf("[wake] quant: input scale=%.6f zp=%d   output scale=%.6f zp=%d\n",
                s_input->params.scale, s_input->params.zero_point, out_scale, out_zp);
  int rawMax = 0;   // diagnostics: peak raw uint8 model output

  // Feature queue + capture task (pinned to core 0). Inference runs here on core 1.
  s_featQ = xQueueCreate(64, kFeatureSize);   // ~640 ms of feature slack
  if (!s_featQ) { Serial.println("[wake] queue alloc FAILED"); while (true) delay(1000); }
  xTaskCreatePinnedToCore(captureTask, "wake-cap", 8192, nullptr, 5, nullptr, 0);

  int   frameIdx = 0;               // 0..kFramesPerInfer within the current inference window
  float probHist[kSlidingWindow] = {0};
  int   probPos = 0;
  uint32_t infers = 0;
  float peakProb = 0;
  int   featMax = INT8_MIN;
  uint32_t maxInferUs = 0;
  int8_t frame[kFeatureSize];

  for (;;) {
    if (xQueueReceive(s_featQ, frame, portMAX_DELAY) != pdTRUE) continue;

    int8_t *slot = s_input->data.int8 + frameIdx * kFeatureSize;
    for (int i = 0; i < kFeatureSize; i++) { slot[i] = frame[i]; if (frame[i] > featMax) featMax = frame[i]; }
    if (++frameIdx < kFramesPerInfer) continue;
    frameIdx = 0;

    uint32_t t0 = micros();
    if (s_interp->Invoke() != kTfLiteOk) { Serial.println("[wake] Invoke failed"); continue; }
    uint32_t inferUs = micros() - t0;
    if (inferUs > maxInferUs) maxInferUs = inferUs;

    int raw = s_output->data.uint8[0];
    if (raw > rawMax) rawMax = raw;
    float prob = (raw - out_zp) * out_scale;
    probHist[probPos] = prob; probPos = (probPos + 1) % kSlidingWindow;
    float avg = 0; for (float p : probHist) avg += p; avg /= kSlidingWindow;
    if (prob > peakProb) peakProb = prob;
    if (prob > 0.1f) Serial.printf("[wake]   raw prob=%.2f (avg=%.2f)\n", prob, avg);

    if (avg >= kProbCutoff) {
      Serial.printf("\n[wake] *** DETECTED \"okay nabu\"  (avg=%.2f) ***\n", avg);
      for (auto &p : probHist) p = 0;
      peakProb = 0;
    }
    if (++infers % 64 == 0) {
      Serial.printf("[wake] listening… peakProb=%.2f rawMax=%d  pcmRms=%.0f  featMax=%d  infer=%luus  drops=%d\n",
                    peakProb, rawMax, s_pcmPeakRms, featMax, (unsigned long)maxInferUs, s_qDrops);
      rawMax = 0;
      // Dump the 40 values of frame 0 in the window we just fed, to eyeball the log-mel spectrum.
      Serial.print("[wake] feat:");
      for (int i = 0; i < kFeatureSize; i++) Serial.printf(" %d", s_input->data.int8[i]);
      Serial.println();
      peakProb = 0; s_pcmPeakRms = 0; featMax = INT8_MIN; maxInferUs = 0;
    }
  }
}
