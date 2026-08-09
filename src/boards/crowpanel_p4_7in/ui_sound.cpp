// UI feedback tones for the CrowPanel Advance 7" — NS4168 class-D amp driving the two onboard
// speakers over I2S. Implements uiSoundPlay() from core/board.h.
//
// Scope on purpose: short synthesised confirmations, nothing else. No decoder, no files, no
// mixing. That is why this does NOT go through the localAudio* contract, which means "play a file
// off local storage" — a thing this unit has no concept of.
//
// Two behaviours that are not obvious and are easy to get wrong:
//
//  0. TONES ARE SYNTHESISED ON A BOARD-OWNED TASK, NOT IN THE CALLER. uiSoundPlay() is called from
//     LVGL event callbacks, i.e. from inside lv_timer_handler() on the UI task. Synthesising there
//     meant the render task sat inside a blocking s_i2s.write() for the whole cue — 75 ms for
//     Confirm, 180 ms for Error, plus the amp's 5 ms settle — and on a panel that renders DIRECT
//     into the live scan-out buffer that is a visible stall in rendering and touch sampling. Now
//     uiSoundPlay() just posts to a queue and returns. The task also owns the idle power-down, so
//     the unit no longer has to call a board function to keep the amp honest.
//  1. THE AMP IS POWERED DOWN WHEN IDLE. PIN_AUDIO_CTRL gates the NS4168; leaving a class-D amp
//     enabled between tones draws current and audibly hisses through the speakers on a quiet
//     wall unit. It is raised just before a tone and dropped again after a short idle delay,
//     rather than per-tone, so a burst of taps does not click the amp on and off repeatedly.
//  2. THE FEEDBACK IS A CLICK, NOT A TONE. See click() below for why that distinction matters and
//     how it is synthesised.
#include <Arduino.h>
#include "core/net/logmirror.h"
#include <ESP_I2S.h>
#include <math.h>

#include "core/board.h"
#include "core/settings.h"
#include "pins.h"

static const uint32_t kSampleRate = 16000;   // ample for sub-2 kHz confirmation tones
static const uint32_t kAmpIdleMs  = 1500;    // power the amp down this long after the last tone

static I2SClass  s_i2s;
static bool      s_ready   = false;
static bool      s_ampOn   = false;
static uint32_t  s_lastMs  = 0;
static QueueHandle_t s_q = nullptr;   // UiSound cues, posted by any task, drained by soundTask

static void soundTask(void *);
static void render(UiSound s, uint8_t vol);

static void ampPower(bool on) {
  if (on == s_ampOn) return;
  s_ampOn = on;
  digitalWrite(PIN_AUDIO_CTRL, on ? AUDIO_POWER_ENABLE : AUDIO_POWER_DISABLE);
  if (on) delay(5);   // let the amp settle before the first sample, or the attack is clipped
}

bool uiSoundInit() {
  pinMode(PIN_AUDIO_CTRL, OUTPUT);
  digitalWrite(PIN_AUDIO_CTRL, AUDIO_POWER_DISABLE);

  s_i2s.setPins(PIN_AUDIO_BCLK, PIN_AUDIO_LRCLK, PIN_AUDIO_SDATA);
  if (!s_i2s.begin(I2S_MODE_STD, kSampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    LOG.println("[uisnd ] I2S begin failed — UI tones disabled");
    return false;
  }
  // Queue depth 4: a burst of taps should not block the caller, and cues older than a few are
  // worthless anyway — a late click is worse than no click.
  s_q = xQueueCreate(4, sizeof(UiSound));
  if (!s_q) { LOG.println("[uisnd ] no queue — UI tones disabled"); return false; }

  // Core 1 (with the UI), priority BELOW the UI task: the render loop must always win, and the
  // sound task only needs to top up the I2S DMA between frames. Core 0 is left to the network.
  if (xTaskCreatePinnedToCore(soundTask, "uisnd", 3072, nullptr, 2, nullptr, 1) != pdPASS) {
    LOG.println("[uisnd ] no task — UI tones disabled");
    return false;
  }

  s_ready = true;
  LOG.println("[uisnd ] NS4168 ready (amp gated ACTIVE LOW, powers down when idle)");

  // Startup chime. Doubles as proof the whole path works — amp enable, I2S, speakers — without
  // needing someone to tap the screen and guess whether silence means "off" or "broken". It goes
  // through the queue like everything else, so the amp is powered down by the task's own idle
  // timeout a moment later. It used to be synthesised inline here, which left s_ampOn true for the
  // whole of appBoot() — through the Wi-Fi connect, and indefinitely on a first boot that sits in
  // the captive portal — hissing the entire time.
  uiSoundPlay(UiSound::Confirm);
  return true;
}

// A CLICK, not a beep. A tone says "a computer noticed"; a click says "a control moved" — which
// is what physical transport caps would give you, and what this unit is standing in for until they
// exist. Acoustically a click is a broadband transient: a burst of noise with a very fast decay.
// A pitched sine, however short, always reads as a beep because its energy sits at one frequency.
//
// Deterministic LCG rather than random(): identical every press, so repeated taps sound like the
// same physical control rather than subtly different ones.
static uint32_t s_rng = 0x13579BDFu;
static inline float noise() {
  s_rng = s_rng * 1664525u + 1013904223u;
  return ((int32_t)(s_rng >> 8) / 8388608.0f) - 1.0f;   // -1..1
}

// ms: total length. decay: how fast it collapses (larger = tighter/sharper click).
// lowpass: 0..1, how much high end to keep — lower is a duller "tock", higher a crisper "tick".
static void click(uint16_t ms, float decay, float lowpass, uint8_t volPct) {
  const uint32_t total = (uint32_t)kSampleRate * ms / 1000;
  const float peak = 32767.0f * 0.7f * (volPct / 100.0f);

  static int16_t buf[256 * 2];
  uint32_t done = 0;
  float lp = 0.0f;

  while (done < total) {
    const uint32_t n = (total - done > 256) ? 256 : (total - done);
    for (uint32_t i = 0; i < n; i++) {
      const uint32_t idx = done + i;
      // Exponential decay, and a one-pole lowpass to shape the timbre.
      const float env = expf(-decay * (float)idx / (float)total);
      lp += lowpass * (noise() - lp);
      const int16_t v = (int16_t)(lp * peak * env);
      buf[i * 2] = v;
      buf[i * 2 + 1] = v;
    }
    s_i2s.write((const uint8_t *)buf, n * 2 * sizeof(int16_t));
    done += n;
  }
}

// A short silence between clicks — otherwise a double-click runs together into one longer noise.
static void gap(uint16_t ms) {
  static int16_t z[128 * 2] = {0};
  uint32_t total = (uint32_t)kSampleRate * ms / 1000;
  while (total) {
    const uint32_t n = total > 128 ? 128 : total;
    s_i2s.write((const uint8_t *)z, n * 2 * sizeof(int16_t));
    total -= n;
  }
}

// Runs on the board's own task. Blocking here is free; blocking in uiSoundPlay() was not.
static void soundTask(void *) {
  for (;;) {
    UiSound s;
    // The 250 ms timeout doubles as the idle-power-down tick, so nothing outside this file has to
    // remember to call anything periodically.
    if (xQueueReceive(s_q, &s, pdMS_TO_TICKS(250)) != pdTRUE) {
      if (s_ampOn && millis() - s_lastMs >= kAmpIdleMs) ampPower(false);
      continue;
    }
    const uint8_t vol = settingsUiSound();
    if (vol == 0) continue;                          // user switched feedback off after queueing
    ampPower(true);
    render(s, vol);
    s_lastMs = millis();
  }
}

// Post and return. Never synthesises, never blocks, never touches the amp — see note 0 at the top.
void uiSoundPlay(UiSound s) {
  if (!s_ready || !s_q) return;
  if (settingsUiSound() == 0) return;                // cheap early out; the task re-checks
  xQueueSend(s_q, &s, 0);                            // drop if full — a late click is worse
}

static void render(UiSound s, uint8_t vol) {
  switch (s) {
    // Crisp single click — a control moved.
    case UiSound::Tick:    click(14, 9.0f, 0.55f, vol); break;
    // Double click, second one brighter: reads as "done" without becoming a melody.
    case UiSound::Confirm: click(14, 9.0f, 0.40f, vol); gap(45); click(16, 8.0f, 0.70f, vol); break;
    // Three dull knocks — clearly negative, still not a beep.
    case UiSound::Error:   click(22, 6.0f, 0.18f, vol); gap(55);
                           click(22, 6.0f, 0.18f, vol); gap(55);
                           click(26, 5.0f, 0.15f, vol); break;
  }
}
