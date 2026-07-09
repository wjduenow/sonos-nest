// ES3C28P local-audio bring-up: play the ocean-sounds MP3 from the microSD through the
// onboard ES8311 codec + speaker. Validates the whole local-playback path (SD -> Helix MP3
// decode -> I2S -> ES8311 -> speaker) in isolation before wiring it into the app as the
// "Play on Device" option. Built only by the `sleep-machine-audio` env (-DAUDIO_BRINGUP).
//
// Uses pschatzmann's arduino-audio-tools + arduino-audio-driver: the driver configures the
// ES8311 over I2C (address/clocks/format/volume) and the AudioTools I2SCodecStream + Helix
// decoder stream the file. SD is read over SDIO 4-bit (SD_MMC), not the MSC bridge.
#include "audio_test.h"
#include "pins.h"
#include <Arduino.h>
#include "SD_MMC.h"
#include "AudioTools.h"
#include "AudioTools/AudioLibs/I2SCodecStream.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"

static const char *OCEAN_FILE =
    "/Ocean Waves Crashing - Relaxing Sounds - Calming Relaxation Music For Sleeping - 1 Hour.mp3";

static DriverDeviceInfo   s_pins;
static AudioBoard         s_board(AudioDriverES8311, s_pins);
static I2SCodecStream     s_out(s_board);
static EncodedAudioStream s_decoder(&s_out, new MP3DecoderHelix());
static StreamCopy         s_copier;
static fs::File           s_file;

static bool openOcean() {
  s_file = SD_MMC.open(OCEAN_FILE);
  if (!s_file) { Serial.printf("[audio] open failed: %s\n", OCEAN_FILE); return false; }
  s_copier.begin(s_decoder, s_file);
  return true;
}

void audioTestRun() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[audio] ES8311 local-playback bring-up");
  AudioDriverLogger.begin(Serial, AudioDriverLogLevel::Info);   // codec init trace

  // Speaker power amp: the board wires it active-low (ESPHome uses inverted:true), so drive
  // LOW to enable. If there's no sound, try HIGH here.
  pinMode(PIN_SPK_ENABLE, OUTPUT);
  digitalWrite(PIN_SPK_ENABLE, LOW);

  // SD card over SDIO 4-bit (read-only; no format on failure).
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
  if (!SD_MMC.begin("/sdcard", false /*4-bit*/, false /*no format*/)) {
    Serial.println("[audio] SD mount FAILED — is a card seated?");
    while (true) delay(1000);
  }
  Serial.printf("[audio] SD mounted, %llu MB\n", SD_MMC.cardSize() / (1024ULL * 1024ULL));

  // Codec pins: I2C control (ES8311 @ 0x18, shared bus at 100 kHz) + I2S data. DO = ESP->codec
  // (playback), DI = codec->ESP (mic, unused here). Pins come from pins.h.
  s_pins.addI2C(PinFunction::CODEC, PIN_I2C_SCL, PIN_I2C_SDA, I2S_ADDR_ES8311, 100000, Wire);
  s_pins.addI2S(PinFunction::CODEC, PIN_I2S_MCLK, PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_DOUT, PIN_I2S_DIN);
  s_pins.begin();
  s_board.begin();

  // I2S + codec output. Pins are taken from the board's DriverDeviceInfo above; here we only
  // set the initial audio format — the MP3 decoder re-applies the file's real rate on the fly.
  auto cfg = s_out.defaultConfig();
  cfg.copyFrom(AudioInfo(44100, 2, 16));
  s_out.begin(cfg);
  s_out.setVolume(0.6f);

  s_decoder.begin();

  if (!openOcean()) { while (true) delay(1000); }
  Serial.printf("[audio] playing %s (%u bytes)\n", OCEAN_FILE, (unsigned)s_file.size());

  while (true) {
    if (!s_copier.copy()) {   // 0 bytes => end of file: loop the track (ocean runs forever)
      Serial.println("[audio] end of track — looping");
      s_file.seek(0);
      s_decoder.begin();
      s_copier.begin(s_decoder, s_file);
    }
  }
}
