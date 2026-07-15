// ES3C28P microphone bring-up — Phase 0 for wake-word / voice control.
//
// Captures PCM from the onboard MEMS mic through the ES8311 codec's ADC and I2S input
// (codec -> ESP over PIN_I2S_DIN), the mirror image of audio_test.cpp's playback path. It does
// three things, in order of why they matter:
//
//   1. Proves the record path works AT ALL — the mic has never been read on this board
//      (CLAUDE.md: "Not yet wired: the mic"). Playback set the DIN pin but never used it.
//   2. Captures at 16 kHz / mono / 16-bit — the exact format every candidate wake-word engine
//      (Espressif WakeNet, Picovoice Porcupine, microWakeWord) consumes. Validating this format
//      now means the capture stage needs no rework when an engine is bolted on.
//   3. Reports free INTERNAL SRAM (not PSRAM) before and after codec init. Internal SRAM (~150
//      KB free) is the binding constraint for on-device wake-word inference; this prints the
//      real headroom on hardware so the engine decision is made on measured numbers, not guesses.
//
// Deliberately RX-ONLY: the codec is set to encode/record mode with no playback stream. Full-
// duplex (listen while the speaker plays) plus acoustic echo cancellation is a later, harder
// phase — Phase 0 isolates capture the same way audio_test.cpp isolated playback.
//
// Uses pschatzmann's arduino-audio-tools + arduino-audio-driver (same stack as local_audio.cpp).
// Built only by the `sleep-machine-mic` env (-DMIC_BRINGUP).
#include "mic_test.h"
#include "pins.h"
#include <Arduino.h>
#include <math.h>
#include "esp_heap_caps.h"
#include <Wire.h>
#include "AudioTools.h"
#include "AudioTools/AudioLibs/I2SCodecStream.h"

// --- Capture format: the wake-word canonical. Do not change without a reason. ---
static const int      MIC_RATE    = 16000;   // 16 kHz — WakeNet/Porcupine/microWakeWord all want this
// Capture STEREO for the bring-up so we can see which I2S slot the ES8311 ADC actually drives:
// the board's ESPHome reference reads the mic on `channel: right`, so a mono (left-slot) capture
// would read a dead slot. Split L/R stats below; once the live slot is known, a shipping build
// captures 1 channel from that slot.
static const int      MIC_CH      = 2;       // L+R interleaved (diagnostic)
static const int      MIC_BITS    = 16;      // signed 16-bit PCM
static const size_t   FRAME_SMPS  = 512;     // 512 samples/read = 32 ms at 16 kHz

static DriverDeviceInfo s_pins;
static AudioBoard        s_board(AudioDriverES8311, s_pins);
static I2SCodecStream    s_in(s_board);

// Poke an ES8311 register directly on the shared I2C bus (codec @ 0x18). Only the codec lives
// on the bus in this bring-up build (no touch controller linked), so raw writes are safe here.
static void es8311Write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(I2S_ADDR_ES8311);
  Wire.write(reg);
  Wire.write(val);
  uint8_t rc = Wire.endTransmission();
  Serial.printf("[mic] ES8311 reg 0x%02X <- 0x%02X  (i2c rc=%u)\n", reg, val, rc);
}

static uint8_t es8311Read(uint8_t reg);   // defined below; used by the ADC bring-up

// Bring up the ES8311 RECORD path with the full sequence from Espressif's esp-adf ES8311 driver
// (es8311_codec_init + es8311_start for ES_MODULE_ADC). The arduino-audio-driver inits the codec
// for PLAYBACK, so it leaves the ADC side down: the analog PGA/ADC unpowered (REG0E=0xFF), the ADC
// clock off (REG01=0x30), the ADC muted onto the serial bus, and the ADC data source unrouted.
// Setting only volume/gain (as the first attempt did) can't produce signal while the analog block
// is powered off — REG0E and the ADC clock are the load-bearing writes.
static void es8311EnableAdc() {
  es8311Write(0x01, 0x3F);   // CLK_MANAGER_REG01: enable ALL clocks incl. the ADC clock (was 0x30)

  // ADC serial-output format: mirror the DAC serial-in format the driver already set for 16-bit
  // I2S (same BCLK/LRCLK), and clear bit6 so the ADC is NOT muted onto SDOUT.
  uint8_t fmt = es8311Read(0x09) & 0x3F;   // REG09 = SDP-in (DAC) format; drop bits 7:6
  es8311Write(0x0A, fmt);                  // REG0A = SDP-out (ADC) format, ADC unmuted (bit6=0)

  // Gain: this mic is low-output, so it needs real gain. Prefer ANALOG PGA (before the ADC, best
  // SNR) maxed out, with digital volume near unity — see the tuning note at the call site.
  es8311Write(0x17, 0xD0);   // ADC_REG17: ADC digital volume ~+8 dB (0xBF=0 dB .. 0xFF=+32 dB)
  es8311Write(0x0E, 0x02);   // *** SYSTEM_REG0E: power UP analog PGA + ADC modulator (was 0xFF=off) ***
  es8311Write(0x12, 0x00);   // SYSTEM_REG12: un-suspend / power up
  es8311Write(0x14, 0x1A);   // SYSTEM_REG14: ANALOG mic input (bit6=0, not DMIC) + PGA gain
  es8311Write(0x0D, 0x01);   // SYSTEM_REG0D: system power up
  es8311Write(0x15, 0x40);   // ADC_REG15: ADC ramp / soft-mute config
  es8311Write(0x16, 0x07);   // ADC_REG16: mic PGA gain — max (~42 dB); analog gain = best SNR
  es8311Write(0x1B, 0x0A);   // ADC_REG1B: ADC HPF/EQ
  es8311Write(0x1C, 0x6A);   // ADC_REG1C: ADC HPF/EQ
  es8311Write(0x13, 0x10);   // SYSTEM_REG13
  es8311Write(0x37, 0x48);   // DAC_REG37: ramp (harmless for record)
  es8311Write(0x45, 0x00);   // GP_REG45
  es8311Write(0x44, 0x50);   // GPIO_REG44: ADC data source = internal ADC (ADCL + DACR)
}

static uint8_t es8311Read(uint8_t reg) {
  Wire.beginTransmission(I2S_ADDR_ES8311);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xEE;   // repeated-start read
  if (Wire.requestFrom((int)I2S_ADDR_ES8311, 1) != 1) return 0xEE;
  return Wire.read();
}

// Dump the record-path registers so we can see the codec's ACTUAL state (vs. what we wrote).
// REG00 is the chip reset/ID-ish status; a live codec answers non-0xEE here.
static void es8311DumpRegs(const char *when) {
  Serial.printf("[mic] regs %-10s R01=0x%02X R0E=0x%02X R0A=0x%02X R14=0x%02X R17=0x%02X R44=0x%02X\n",
                when, es8311Read(0x01), es8311Read(0x0E), es8311Read(0x0A),
                es8311Read(0x14), es8311Read(0x17), es8311Read(0x44));
}

static void printHeap(const char *when) {
  Serial.printf("[mic] heap %-12s  internal free=%6u  largest-block=%6u   PSRAM free=%8u\n",
                when,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)ESP.getFreePsram());
}

// A crude 40-column VU bar so the mic is obviously alive on the serial monitor: tap the mic or
// talk and the bar jumps. `rms` is 0..32767; map log-ish so quiet room speech is visible.
static void printMeter(int16_t peak, float rms, float dc) {
  const int W = 40;
  int filled = 0;
  if (rms > 1.0f) {
    // 20*log10(rms/32767) spans about -90..0 dBFS; show the top 60 dB across the bar.
    float dbfs = 20.0f * log10f(rms / 32767.0f);
    filled = (int)((dbfs + 60.0f) / 60.0f * W);
    if (filled < 0) filled = 0;
    if (filled > W) filled = W;
  }
  char bar[W + 1];
  for (int i = 0; i < W; i++) bar[i] = i < filled ? '#' : '.';
  bar[W] = '\0';
  Serial.printf("[mic] |%s| peak=%6d rms=%7.1f dc=%7.1f\n", bar, peak, rms, dc);
}

void micTestRun() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[mic] ES8311 microphone capture bring-up (16 kHz mono 16-bit)");
  AudioDriverLogger.begin(Serial, AudioDriverLogLevel::Info);   // codec init trace over I2C

  printHeap("at boot");

  // Codec pins: I2C control (ES8311 @ 0x18, shared 100 kHz bus) + I2S data. Same wiring as
  // playback; the direction that matters here is DI = codec->ESP (mic), on PIN_I2S_DIN.
  s_pins.addI2C(PinFunction::CODEC, PIN_I2C_SCL, PIN_I2C_SDA, I2S_ADDR_ES8311, 100000, Wire);
  s_pins.addI2S(PinFunction::CODEC, PIN_I2S_MCLK, PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_DOUT, PIN_I2S_DIN);
  s_pins.begin();
  s_board.begin();

  // RX_MODE puts the ES8311 in encode/record mode and opens the I2S peripheral for input only.
  auto cfg = s_in.defaultConfig(RX_MODE);
  cfg.copyFrom(AudioInfo(MIC_RATE, MIC_CH, MIC_BITS));
  // ES8311 has a single differential mic input. If the meter stays flat, the two things to try
  // first are a different input_device selection here and the mic PGA gain below.
  cfg.input_device = ADC_INPUT_LINE1;
  if (!s_in.begin(cfg)) {
    Serial.println("[mic] I2SCodecStream begin() FAILED — codec/I2S did not init");
    while (true) delay(1000);
  }

  // Mic PGA gain. The driver's default can be conservative; bump toward the codec's max if room
  // speech barely moves the bar. (arduino-audio-driver exposes this via the board's driver.)
  s_board.setInputVolume(90);   // 0..100; raise to 100 or lower if it clips (peak pinned at 32767)
  es8311EnableAdc();            // full esp-adf record-path bring-up (analog power-up is the key)

  printHeap("after codec");
  Serial.printf("[mic] capturing %u-sample frames (%u ms each). Tap the mic / talk — the bar should move.\n",
                (unsigned)FRAME_SMPS, (unsigned)(FRAME_SMPS * 1000 / MIC_RATE));

  static int16_t frame[FRAME_SMPS];
  uint32_t frameCount = 0;

  for (;;) {
    size_t want = sizeof(frame);
    size_t got  = s_in.readBytes((uint8_t *)frame, want);
    if (got == 0) {
      // No data — I2S starved. Shouldn't happen once running; log and keep trying.
      Serial.println("[mic] read 0 bytes (I2S starved?)");
      delay(100);
      continue;
    }
    size_t n = got / sizeof(int16_t);   // total int16 samples, L/R interleaved

    // Separate stats per I2S slot: even indices = LEFT, odd = RIGHT. Whichever slot's RMS jumps
    // when you talk is where the ES8311 ADC actually drives the mic.
    double sumsqL = 0, sumsqR = 0;
    int16_t peakL = 0, peakR = 0;
    size_t nL = 0, nR = 0;
    for (size_t i = 0; i < n; i++) {
      int16_t s = frame[i];
      if (i & 1) { sumsqR += (double)s * s; if (abs(s) > peakR) peakR = abs(s); nR++; }
      else       { sumsqL += (double)s * s; if (abs(s) > peakL) peakL = abs(s); nL++; }
    }
    float rmsL = nL ? sqrtf((float)(sumsqL / (double)nL)) : 0;
    float rmsR = nR ? sqrtf((float)(sumsqR / (double)nR)) : 0;

    // ~4 lines/sec keeps the serial readable (one line per ~8 frames = ~256 ms).
    if ((frameCount % 8) == 0)
      Serial.printf("[mic] L: peak=%5d rms=%6.1f   |   R: peak=%5d rms=%6.1f\n",
                    peakL, rmsL, peakR, rmsR);
    // Reprint the SRAM headroom every ~5 s. Repeating (vs a one-shot at boot) survives the
    // USB-CDC re-enumeration gap — CDC drops prints emitted before the host opens the port — and
    // doubles as a leak watch: the internal-free number should hold steady while capturing.
    if ((frameCount % 160) == 0) { printHeap("(running)"); es8311DumpRegs("(running)"); }
    frameCount++;
  }
}
