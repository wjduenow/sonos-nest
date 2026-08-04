// microSD mount for the CrowPanel Advance 7" (ESP32-P4). See sd_card.h.
//
// This is the shipping counterpart of the jukebox-sd bring-up probe, and it inherits everything
// that probe established the hard way. The short version, because getting any of it wrong produces
// a plausible-looking failure rather than an error:
//
//   * Slot 0, 1-bit, 10 MHz, CLK 43 / CMD 44 / D0 39. The C6 radio is on slot 1 — independent
//     controller, so card traffic costs the radio nothing. Do not touch slot 1 from here.
//   * NO power-control handle. esp_ldo_acquire_channel(4) fails on this board, and card init
//     succeeds without one because the slot's I/O rail is board-powered.
//   * NEVER use Arduino's SD_MMC: it takes its pins and a power-enable pin from the board variant,
//     whose stock BOARD_SDMMC_POWER_PIN 45 is this board's I2C SDA. It would kill touch. See pins.h.
//   * FATFS must be CONFIG_FATFS_SECTOR_512 (set in platformio.ini). The inherited default was
//     SECTOR_4096, a SPI-flash option, which makes every LBA 8x wrong and shows up as
//     sdmmc_write_blocks timeouts that look exactly like bad hardware.
//   * format_if_mount_failed is FALSE and must stay that way. A controller that silently eats the
//     owner's card on a bad boot is not acceptable behaviour.
#include <Arduino.h>

#include <driver/sdmmc_host.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

#include "core/board.h"
#include "pins.h"
#include "sd_card.h"

#define SD_MOUNT_POINT "/sdcard"

static sdmmc_card_t *s_card = nullptr;

bool sdCardInit() {
  if (s_card) return true;

  esp_vfs_fat_sdmmc_mount_config_t mcfg = {};
  mcfg.format_if_mount_failed = false;   // see the header comment — never reformat the owner's card
  mcfg.max_files              = 8;
  mcfg.allocation_unit_size   = 0;       // let FatFs choose; forcing a size made f_mkfs reject it

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot         = SD_SLOT;
  host.max_freq_khz = SD_FREQ_KHZ;

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk    = (gpio_num_t)PIN_SD_CLK;
  slot.cmd    = (gpio_num_t)PIN_SD_CMD;
  slot.d0     = (gpio_num_t)PIN_SD_D0;
  slot.width  = 1;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  const esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mcfg, &s_card);
  if (err != ESP_OK) {
    s_card = nullptr;
    // Distinguish the two failures worth acting on: no card at all vs a card we cannot read.
    if (err == ESP_FAIL)
      Serial.println("[sd    ] card present but no FAT32 filesystem — format it FAT32 (not exFAT)");
    else
      Serial.printf("[sd    ] no card (%s)\n", esp_err_to_name(err));
    return false;
  }
  Serial.printf("[sd    ] mounted %s — %s, %llu MB\n", SD_MOUNT_POINT, s_card->cid.name,
                ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) >> 20);
  return true;
}

bool sdCardMounted() { return s_card != nullptr; }

uint64_t sdCardFreeBytes() {
  if (!s_card) return 0;
  FATFS *fs = nullptr;
  DWORD  clusters = 0;
  if (f_getfree("0:", &clusters, &fs) != FR_OK || !fs) return 0;
  return (uint64_t)clusters * fs->csize * FF_MIN_SS;
}

// --- core/board.h -------------------------------------------------------------------------------
const char *localStorageRoot() { return s_card ? SD_MOUNT_POINT : nullptr; }
