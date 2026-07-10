#include "sd_card.h"
#include "pins.h"
#include <Arduino.h>
#include "SD_MMC.h"

bool sdEnsureMounted() {
  static bool mounted = false;
  if (mounted) return true;
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
  mounted = SD_MMC.begin("/sdcard", false /*4-bit*/, false /*no format*/);
  if (!mounted) { Serial.println("[sd] mount failed — is a card seated?"); return false; }

  // One-time migration: shorten the original long ocean filename to /Ocean.mp3. Idempotent
  // (only fires while the old file still exists and the new one doesn't).
  static const char *kOldOcean =
      "/Ocean Waves Crashing - Relaxing Sounds - Calming Relaxation Music For Sleeping - 1 Hour.mp3";
  if (SD_MMC.exists(kOldOcean) && !SD_MMC.exists("/Ocean.mp3")) {
    if (SD_MMC.rename(kOldOcean, "/Ocean.mp3")) Serial.println("[sd] renamed ocean track -> /Ocean.mp3");
  }
  return true;
}
