#include "sd_card.h"
#include "pins.h"
#include <Arduino.h>
#include "SD_MMC.h"

bool sdEnsureMounted() {
  static bool mounted = false;
  if (mounted) return true;
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
  mounted = SD_MMC.begin("/sdcard", false /*4-bit*/, false /*no format*/);
  if (!mounted) Serial.println("[sd] mount failed — is a card seated?");
  return mounted;
}
