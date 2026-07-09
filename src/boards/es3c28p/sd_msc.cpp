// ES3C28P SD -> USB Mass Storage bridge (bring-up mode).
//
// Exposes the inserted microSD to the host over the native USB port as a removable drive, so
// the Nursery ocean-sounds track (and future local content) can be copied onto the card
// without removing it. Built only by the `sleep-machine-sdreader` env, which sets
// -DSD_MSC_MODE and -DARDUINO_USB_MODE=0 (TinyUSB OTG — required for USB MSC; it replaces the
// USB-Serial-JTAG that normal builds flash over).
//
// Serves raw 512-byte sectors straight from the IDF sdmmc driver (SDIO 4-bit, pins in pins.h)
// and deliberately does NOT mount FatFS on the ESP32 side: the host owns the filesystem, and
// a second concurrent mount would corrupt it. The card already carries a FAT32 volume.
#include "sd_msc.h"
#include "pins.h"
#include <Arduino.h>
#include "USB.h"
#include "USBMSC.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

static sdmmc_card_t *s_card = nullptr;
static USBMSC s_msc;

// MSC block callbacks. Transfers are sector-aligned (block size = 512), so offset is 0.
static int32_t mscRead(uint32_t lba, uint32_t /*offset*/, void *buffer, uint32_t bufsize) {
  if (!s_card) return -1;
  uint32_t count = bufsize / 512;
  if (sdmmc_read_sectors(s_card, buffer, lba, count) != ESP_OK) return -1;
  return (int32_t)(count * 512);
}

static int32_t mscWrite(uint32_t lba, uint32_t /*offset*/, uint8_t *buffer, uint32_t bufsize) {
  if (!s_card) return -1;
  uint32_t count = bufsize / 512;
  if (sdmmc_write_sectors(s_card, buffer, lba, count) != ESP_OK) return -1;
  return (int32_t)(count * 512);
}

static bool mscStartStop(uint8_t /*power*/, bool /*start*/, bool /*load_eject*/) { return true; }

static bool sdInit() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 4;
  slot.clk = (gpio_num_t)PIN_SD_CLK;
  slot.cmd = (gpio_num_t)PIN_SD_CMD;
  slot.d0  = (gpio_num_t)PIN_SD_D0;
  slot.d1  = (gpio_num_t)PIN_SD_D1;
  slot.d2  = (gpio_num_t)PIN_SD_D2;
  slot.d3  = (gpio_num_t)PIN_SD_D3;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;  // backstop; board carries its own pullups

  if (sdmmc_host_init() != ESP_OK) return false;
  if (sdmmc_host_init_slot(host.slot, &slot) != ESP_OK) return false;
  s_card = (sdmmc_card_t *)malloc(sizeof(sdmmc_card_t));
  if (!s_card) return false;
  if (sdmmc_card_init(&host, s_card) != ESP_OK) { free(s_card); s_card = nullptr; return false; }
  return true;
}

void sdMscRun() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n[sd-msc] ES3C28P SD -> USB mass-storage bridge");

  if (!sdInit()) {
    Serial.println("[sd-msc] SD init FAILED — is a card seated? (SDIO 4-bit)");
    while (true) delay(1000);
  }

  uint32_t sectorSize  = s_card->csd.sector_size;   // 512
  uint32_t sectorCount = s_card->csd.capacity;      // total sector count
  uint64_t mb = ((uint64_t)sectorCount * sectorSize) / (1024ULL * 1024ULL);
  Serial.printf("[sd-msc] card OK: %llu MB (%u sectors x %u bytes)\n",
                mb, (unsigned)sectorCount, (unsigned)sectorSize);

  s_msc.vendorID("Sonos");
  s_msc.productID("Nursery SD");
  s_msc.productRevision("1.0");
  s_msc.onRead(mscRead);
  s_msc.onWrite(mscWrite);
  s_msc.onStartStop(mscStartStop);
  s_msc.mediaPresent(true);
  s_msc.begin(sectorCount, sectorSize);

  USB.begin();
  Serial.println("[sd-msc] USB drive presented. Copy files on the host, then EJECT before unplug.");
  while (true) delay(1000);
}
