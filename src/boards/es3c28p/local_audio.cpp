// ES3C28P local-audio player — implements the board HAL's localAudio* (core/board.h) for the
// app. Plays a file off the microSD through the onboard ES8311 codec + speaker: SD_MMC (SDIO
// 4-bit) -> Helix MP3 decode -> I2S -> ES8311. Same pipeline as audio_test.cpp, but start/stop-
// able and driven by a board-owned FreeRTOS task so the UI just posts intent.
//
// The heavy lifting (SD mount + codec init) is lazy: it runs on the first localAudioPlay() so
// boot stays fast and the codec's I2C traffic doesn't race the display/touch bring-up. The
// ES8311 shares the I2C bus (100 kHz) already begun by boardInit().
#include "core/board.h"
#include "pins.h"
#include "sd_card.h"
#include <Arduino.h>
#include "SD_MMC.h"
#include "AudioTools.h"
#include "AudioTools/AudioLibs/I2SCodecStream.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"

static DriverDeviceInfo   s_pins;
static AudioBoard         s_board(AudioDriverES8311, s_pins);
static I2SCodecStream     s_out(s_board);
// Software volume before the codec: the ES8311's own volume register doesn't take on this
// board, so we scale the PCM here instead. VolumeStream(AudioStream&) also forwards the
// decoder's sample-rate changes on to the codec.
static VolumeStream       s_vol(s_out);
static EncodedAudioStream s_decoder(&s_vol, new MP3DecoderHelix());
static StreamCopy         s_copier;
static fs::File           s_file;

static volatile bool s_active = false;   // a file is currently streaming
static volatile bool s_loop   = true;    // restart at EOF (ambient sound runs continuously)
static bool          s_inited = false;   // SD + codec brought up yet?
static TaskHandle_t  s_task   = nullptr;

static bool ensureInit() {
  if (s_inited) return true;

  if (!sdEnsureMounted()) return false;

  // ES8311 @ 0x18 on the shared I2C bus (already begun at 100 kHz by boardInit()); I2S data.
  s_pins.addI2C(PinFunction::CODEC, PIN_I2C_SCL, PIN_I2C_SDA, I2S_ADDR_ES8311, 100000, Wire);
  s_pins.addI2S(PinFunction::CODEC, PIN_I2S_MCLK, PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_DOUT, PIN_I2S_DIN);
  s_pins.begin();
  s_board.begin();

  auto cfg = s_out.defaultConfig();
  cfg.copyFrom(AudioInfo(44100, 2, 16));
  s_out.begin(cfg);
  s_out.setVolume(1.0f);   // codec at full; the software VolumeStream sets the actual level

  auto vcfg = s_vol.defaultConfig();
  vcfg.copyFrom(AudioInfo(44100, 2, 16));
  s_vol.begin(vcfg);
  s_vol.setVolume(0.6f);   // default 60% (matches the slider's default)

  // Speaker power amp: active-low enable. Keep it OFF (HIGH) until we're actually playing so
  // there's no idle hiss.
  pinMode(PIN_SPK_ENABLE, OUTPUT);
  digitalWrite(PIN_SPK_ENABLE, HIGH);

  s_inited = true;
  return true;
}

// Pinned to core 1 (with the UI). copy() blocks on the I2S DMA between chunks, so it yields
// the CPU and doesn't starve LVGL; when idle it just sleeps.
static void audioTask(void *) {
  for (;;) {
    if (s_active) {
      if (!s_copier.copy()) {           // 0 bytes => end of file
        if (s_loop && s_file) {
          s_file.seek(0);
          s_decoder.begin();
          s_copier.begin(s_decoder, s_file);
        } else {
          localAudioStop();
        }
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

bool localAudioPlay(const char *path) {
  if (!ensureInit()) return false;
  if (s_active) localAudioStop();

  s_file = SD_MMC.open(path);
  if (!s_file) { Serial.printf("[audio] open failed: %s\n", path); return false; }

  s_decoder.begin();
  s_copier.begin(s_decoder, s_file);
  digitalWrite(PIN_SPK_ENABLE, LOW);    // enable the speaker amp
  s_active = true;

  if (!s_task) {
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 2, &s_task, 1);
  }
  Serial.printf("[audio] playing %s\n", path);
  return true;
}

void localAudioStop() {
  s_active = false;
  digitalWrite(PIN_SPK_ENABLE, HIGH);   // mute the amp
  if (s_file) s_file.close();
}

bool localAudioActive() { return s_active; }

void localAudioSetVolume(uint8_t pct) {
  if (!s_inited) return;                 // pipeline not up yet (only playing has a slider)
  if (pct > 100) pct = 100;
  s_vol.setVolume(pct / 100.0f);         // software gain before the codec
}
