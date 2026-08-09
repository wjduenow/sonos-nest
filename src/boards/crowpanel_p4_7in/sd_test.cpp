// microSD bring-up + throughput probe for the CrowPanel Advance 7" (ESP32-P4).
//   pio run -e jukebox-sd -t upload --upload-port /dev/ttyUSB0   # then POWER CYCLE
//   python3 tools/readser.py /dev/ttyUSB0 90
//
// WHY THIS EXISTS. The jukebox re-downloads every album cover over the ESP-Hosted link — 228 KB in
// 1353 ms, measured — and that link is the one unresolved fault on this board (it dies under load,
// plans/07). The card sits on SDMMC *slot 0* while the C6 sits on SDIO *slot 1*, so cache reads
// cost the radio nothing. But "cache it on the card" is only an improvement if the card is
// actually faster than the network, and nobody has measured that on this hardware. This probe
// answers exactly that, and nothing else. Standalone (no core/, no LVGL, no Wi-Fi), same shape as
// display_test.cpp / multicast_test.cpp.
//
// It deliberately uses the IDF sdmmc API rather than Arduino's SD_MMC — see the warning in pins.h.
// SD_MMC would take its pins and its power-enable pin from the board variant, and ours still
// carries Espressif EV-board values, one of which (GPIO45) is this board's I2C SDA.
#include <Arduino.h>

#include <driver/sdmmc_host.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <sys/stat.h>
#include <sys/unistd.h>

// The P4 reports SOC_SDMMC_IO_POWER_EXTERNAL, meaning the slot's I/O rail is not powered by the
// core supply. Elecrow's own example never sets up a power-control handle, so attempt 1 matches
// them exactly; attempt 2 adds an on-chip LDO in case IDF 5.5.5 is stricter than the 5.4 they
// targeted. Testing both in one flash beats one theory per upload — and per plans/07 lesson 1, a
// negative result from a single attempt on this board is not worth much on its own.
#if __has_include("sd_pwr_ctrl_by_on_chip_ldo.h")
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#define HAVE_ON_CHIP_LDO 1
#endif

#include "pins.h"

// A macro, not a const char* — the paths below are built by literal concatenation (MOUNT "/x.bin").
#define MOUNT "/sdcard"

// Sized to the thing this is really about: one decoded cover at the jukebox's ART_MAX_PX=280,
// RGB565 double-byte => 280*280*2 = 156,800 B. If a read of this lands well under the 1353 ms the
// network fetch takes, an art cache is worth building; if it doesn't, this whole direction dies
// here and that is a useful answer too.
static const size_t ART_BLOB = 280 * 280 * 2;
static const size_t CHUNK    = 32 * 1024;      // realistic app-sized I/O, not a synthetic 1-byte loop

static sdmmc_card_t *s_card = nullptr;
static uint8_t      *s_buf  = nullptr;   // PSRAM I/O buffer
static uint8_t      *s_int  = nullptr;   // internal DMA-capable I/O buffer (for the A/B below)

// Set before a (re)mount. 0 = the driver's default. A card whose multi-block write busy period
// exceeds the default shows up as sdmmc_wait_for_idle timing out — which is exactly what this
// board does on 32 KB writes while 4 KB writes succeed, so it is the first thing to vary.
static int s_cmdTimeoutMs = 0;

// freqKhz lets the caller step the clock down: signal-integrity faults on a bus you cannot
// re-route usually show up as "works at 400 kHz, fails at 10 MHz", which is a different fix from
// "wrong pins" and worth separating before anyone reaches for a scope.
static bool mountCard(bool withLdo, int freqKhz) {
  esp_vfs_fat_sdmmc_mount_config_t mcfg = {};
  // FORMATTING IS OPT-IN AT BUILD TIME AND DESTROYS THE CARD'S CONTENTS. The default is off so a
  // bring-up probe can never eat somebody's card by being run; the env turns it on deliberately,
  // once, with the owner's go-ahead. Drop -DSD_ALLOW_FORMAT again once the card is FAT32.
#ifdef SD_ALLOW_FORMAT
  mcfg.format_if_mount_failed = true;
#else
  mcfg.format_if_mount_failed = false;
#endif
  mcfg.max_files              = 5;
  // 0 = let FatFs pick the cluster size. Forcing 16 KB made f_mkfs fail in 21 ms — too fast to be
  // I/O, and the card verifies raw writes across its whole address range, so the rejection is of
  // the parameter, not the medium. On a 64 GB volume FatFs wants a say in the geometry.
  mcfg.allocation_unit_size   = 0;

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot         = SD_SLOT;            // slot 0 — slot 1 is the C6, do not touch it
  host.max_freq_khz = freqKhz;
  if (s_cmdTimeoutMs) host.command_timeout_ms = s_cmdTimeoutMs;

  if (withLdo) {
#ifdef HAVE_ON_CHIP_LDO
    // Internal LDO channel only — this configures an on-die regulator, it does NOT drive a GPIO.
    // Nothing here goes near GPIO45 (the variant's bogus BOARD_SDMMC_POWER_PIN, which is this
    // board's I2C SDA). See the warning in pins.h.
    sd_pwr_ctrl_ldo_config_t ldo = {};
    ldo.ldo_chan_id = 4;                  // stock esp32p4 variant's BOARD_SDMMC_POWER_CHANNEL
    sd_pwr_ctrl_handle_t h = nullptr;
    const esp_err_t lerr = sd_pwr_ctrl_new_on_chip_ldo(&ldo, &h);
    if (lerr != ESP_OK) {
      Serial.printf("[sd]   LDO chan 4 setup failed: %s\n", esp_err_to_name(lerr));
      return false;
    }
    host.pwr_ctrl_handle = h;
#else
    Serial.println("[sd]   (no sd_pwr_ctrl_by_on_chip_ldo.h in this IDF — skipping LDO attempt)");
    return false;
#endif
  }

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk    = (gpio_num_t)PIN_SD_CLK;
  slot.cmd    = (gpio_num_t)PIN_SD_CMD;
  slot.d0     = (gpio_num_t)PIN_SD_D0;
  slot.width  = 1;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  Serial.printf("[sd] try: slot %d  clk=%d cmd=%d d0=%d  width=1  %d kHz  ldo=%s\n",
                SD_SLOT, PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, freqKhz, withLdo ? "chan4" : "none");

  const esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT, &host, &slot, &mcfg, &s_card);
  if (err != ESP_OK) {
    Serial.printf("[sd]   FAIL: %s (0x%x)\n", esp_err_to_name(err), err);
    if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FOUND)
      Serial.println("[sd]     card never answered — not seated, unpowered, or wrong pins.");
    if (err == ESP_FAIL)
      Serial.println("[sd]     card answered but no FAT filesystem — reformat FAT32 (not exFAT).");
    if (err == ESP_ERR_INVALID_STATE)
      Serial.println("[sd]     already mounted, or the host was left initialised by a prior try.");
    sdmmc_host_deinit();   // leave the slot clean so the next attempt starts from a known state
    s_card = nullptr;
    return false;
  }
  Serial.println("[sd]   MOUNTED");
  sdmmc_card_print_info(stdout, s_card);
  return true;
}

// MEASURED: attempt 1 is enough. The card enumerates at the full 10 MHz on the first try, and the
// LDO variants are gone because the board answered that question — esp_ldo_acquire_channel(4)
// fails here ("already in use by others or not adjustable"), and card init succeeds without any
// power-control handle, so this slot's I/O rail is powered by the board. The 400 kHz fallback stays
// as a cheap discriminator: if this ever starts failing, "works slow, fails fast" is a signal-
// integrity fault and a different problem from "never answers".
static bool tryAllMounts() {
  if (mountCard(false, SD_FREQ_KHZ)) return true;
  if (mountCard(false, 400))         return true;
  return false;
}

// Returns throughput in KB/s, and reports the wall-clock ms — the ms is the number that matters
// for the art path, the KB/s is what generalises to other blob sizes.
static void timeWrite(const char *path, size_t bytes, uint8_t *buf, size_t chunk) {
  FILE *f = fopen(path, "wb");
  if (!f) { Serial.printf("[sd] FAIL open %s for write: %s\n", path, strerror(errno)); return; }
  const uint32_t t0 = millis();
  size_t left = bytes;
  while (left) {
    const size_t n = left < chunk ? left : chunk;
    if (fwrite(buf, 1, n, f) != n) { Serial.println("[sd] FAIL short write"); fclose(f); return; }
    left -= n;
  }
  fflush(f);
  fsync(fileno(f));   // without this the timing measures the VFS cache, not the card
  fclose(f);
  const uint32_t ms = millis() - t0;
  Serial.printf("[sd] write %6u B in %5lu ms  = %5lu KB/s\n", (unsigned)bytes,
                (unsigned long)ms, (unsigned long)(ms ? (bytes / ms) : 0));
}

static void timeRead(const char *path, size_t bytes, uint8_t *buf, size_t chunk) {
  FILE *f = fopen(path, "rb");
  if (!f) { Serial.printf("[sd] FAIL open %s for read: %s\n", path, strerror(errno)); return; }
  const uint32_t t0 = millis();
  size_t got = 0, n;
  while ((n = fread(buf, 1, chunk, f)) > 0) got += n;
  fclose(f);
  const uint32_t ms = millis() - t0;
  Serial.printf("[sd] read  %6u B in %5lu ms  = %5lu KB/s%s\n", (unsigned)got,
                (unsigned long)ms, (unsigned long)(ms ? (got / ms) : 0),
                got == bytes ? "" : "   *** SHORT READ ***");
}

static void unmountCard() {
  if (!s_card) return;
  esp_vfs_fat_sdcard_unmount(MOUNT, s_card);
  s_card = nullptr;
}

// Correctness before speed: a cache that returns wrong bytes is worse than no cache.
static bool verifyRoundTrip() {
  const char *p = MOUNT "/verify.bin";
  const size_t n = 4096;
  for (size_t i = 0; i < n; ++i) s_buf[i] = (uint8_t)(i * 31 + 7);

  FILE *f = fopen(p, "wb");
  if (!f) { Serial.println("[sd] FAIL verify: open for write"); return false; }
  fwrite(s_buf, 1, n, f);
  fclose(f);

  memset(s_buf, 0, n);
  f = fopen(p, "rb");
  if (!f) { Serial.println("[sd] FAIL verify: open for read"); return false; }
  const size_t got = fread(s_buf, 1, n, f);
  fclose(f);

  if (got != n) { Serial.printf("[sd] FAIL verify: read %u of %u\n", (unsigned)got, (unsigned)n); return false; }
  for (size_t i = 0; i < n; ++i) {
    if (s_buf[i] != (uint8_t)(i * 31 + 7)) {
      Serial.printf("[sd] FAIL verify: byte %u = %02x, expected %02x\n",
                    (unsigned)i, s_buf[i], (uint8_t)(i * 31 + 7));
      return false;
    }
  }
  unlink(p);
  Serial.println("[sd] verify round-trip OK (4 KB written, read back, byte-exact)");
  return true;
}

// --- Raw-block probe, below the filesystem ------------------------------------------------------
// The VFS helper collapses three very different failures into one ESP_FAIL: card doesn't answer,
// card answers but reads fail, card reads but writes fail. Writes timing out while init succeeds is
// the interesting case (it is what this board does), and no amount of FAT-level retrying explains
// it. So talk to the card directly: identify it, read sector 0, then write and read back a scratch
// sector. Whatever breaks, breaks somewhere nameable.
static sdmmc_card_t s_raw;

static void dumpBootSector(const uint8_t *s) {
  Serial.printf("[raw] sector0: %02x %02x %02x  oem='%.8s'  sig=%02x%02x\n",
                s[0], s[1], s[2], (const char *)(s + 3), s[510], s[511]);
  if (!memcmp(s + 3, "EXFAT   ", 8))        Serial.println("[raw]   -> exFAT (IDF's FATFS cannot read this)");
  else if (!memcmp(s + 0x52, "FAT32", 5))   Serial.println("[raw]   -> FAT32");
  else if (!memcmp(s + 0x36, "FAT", 3))     Serial.println("[raw]   -> FAT12/16");
  else if (s[510] == 0x55 && s[511] == 0xAA) Serial.println("[raw]   -> MBR partition table (fs is inside a partition)");
  else                                       Serial.println("[raw]   -> no recognisable boot sector");
}

static void probeRaw() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot         = SD_SLOT;
  host.max_freq_khz = SD_FREQ_KHZ;

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk    = (gpio_num_t)PIN_SD_CLK;
  slot.cmd    = (gpio_num_t)PIN_SD_CMD;
  slot.d0     = (gpio_num_t)PIN_SD_D0;
  slot.width  = 1;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  esp_err_t e = sdmmc_host_init();
  if (e != ESP_OK) { Serial.printf("[raw] host_init: %s\n", esp_err_to_name(e)); return; }
  e = sdmmc_host_init_slot(SD_SLOT, &slot);
  if (e != ESP_OK) { Serial.printf("[raw] init_slot: %s\n", esp_err_to_name(e)); sdmmc_host_deinit(); return; }
  e = sdmmc_card_init(&host, &s_raw);
  if (e != ESP_OK) {
    Serial.printf("[raw] card_init: %s — the card never enumerated\n", esp_err_to_name(e));
    sdmmc_host_deinit();
    return;
  }
  Serial.println("[raw] card_init OK");
  sdmmc_card_print_info(stdout, &s_raw);

  // 512 B of internal DMA-capable RAM — keep the transfer itself boring so the result is about the
  // card, not about PSRAM DMA. (PSRAM DMA gets exercised later, by the real throughput runs.)
  uint8_t *sec = (uint8_t *)heap_caps_malloc(512, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!sec) { Serial.println("[raw] no DMA buffer"); sdmmc_host_deinit(); return; }

  e = sdmmc_read_sectors(&s_raw, sec, 0, 1);
  Serial.printf("[raw] read sector 0: %s\n", esp_err_to_name(e));
  if (e == ESP_OK) dumpBootSector(sec);

  // *** RAW SECTOR WRITES ARE DESTRUCTIVE AND ARE NOW OPT-IN. ***
  // These write test patterns at fixed LBAs with no regard for what is on the card. LBA 2048 is
  // where Windows puts the first partition's FAT32 boot sector on a 1 MiB-aligned card, so this
  // sweep DESTROYS a freshly formatted filesystem — and because loop() retries, it did so every
  // 10 seconds. It was written when the card was known-blank and that assumption silently expired.
  // Raw reads below stay unconditional; only writes are gated.
#ifdef SD_ALLOW_RAW_WRITE
  const uint32_t scratch = (s_raw.csd.capacity > 64) ? (s_raw.csd.capacity - 64) : 1;
#ifdef SD_ALLOW_RAW_WRITE
  for (int i = 0; i < 512; ++i) sec[i] = (uint8_t)(i ^ 0x5A);
  e = sdmmc_write_sectors(&s_raw, sec, scratch, 1);
  Serial.printf("[raw] write sector %lu: %s\n", (unsigned long)scratch, esp_err_to_name(e));
  if (e == ESP_OK) {
    memset(sec, 0, 512);
    e = sdmmc_read_sectors(&s_raw, sec, scratch, 1);
    bool ok = (e == ESP_OK);
    for (int i = 0; ok && i < 512; ++i) ok = (sec[i] == (uint8_t)(i ^ 0x5A));
    Serial.printf("[raw] read back: %s%s\n", esp_err_to_name(e), ok ? " — byte-exact, THE CARD IS WRITABLE" : " — MISMATCH");
  } else {
    Serial.println("[raw]   writes fail while init+read succeed: card locked/worn, or DAT-line/power");
    Serial.println("[raw]   fault. Try the card in a PC — if it won't format there either, it's the card.");
  }
#else
  (void)scratch;
#endif

  // --- Multi-block sweep, below FATFS -----------------------------------------------------------
  // 4 KB writes through the filesystem work and 32 KB writes don't, with neither the buffer's
  // memory nor the command timeout making any difference. That leaves the transfer size itself —
  // but "size" could mean FATFS/VFS chopping the request badly, or the sdmmc driver and card
  // disagreeing about multi-block writes. Same sweep with no filesystem in the path answers which,
  // and that decides whether the cache can use plain files or has to own raw sectors.
  // --- Where on the card do writes work? ---------------------------------------------------------
  // Everything raw that has passed so far wrote near the END of the card; everything that fails
  // (FAT tables, f_mkfs, the data area of a fresh filesystem) lives near the BEGINNING. That is a
  // difference nobody has controlled for, and it separates "defective card" from "software" more
  // cheaply than any further driver tuning. Write a pattern, read it back, compare.
  Serial.println("[raw] write+verify across the address range:");
  {
    const uint32_t cap = s_raw.csd.capacity;
    const uint32_t spots[] = {2048, 65536, 1000000, cap / 2, cap - 1024};
    for (uint32_t lba : spots) {
      for (int i = 0; i < 512; ++i) sec[i] = (uint8_t)(i ^ (lba & 0xFF));
      const esp_err_t we = sdmmc_write_sectors(&s_raw, sec, lba, 1);
      bool match = false;
      if (we == ESP_OK) {
        memset(sec, 0, 512);
        if (sdmmc_read_sectors(&s_raw, sec, lba, 1) == ESP_OK) {
          match = true;
          for (int i = 0; match && i < 512; ++i) match = (sec[i] == (uint8_t)(i ^ (lba & 0xFF)));
        }
      }
      Serial.printf("[raw]   lba %10lu: write %-16s readback %s\n", (unsigned long)lba,
                    esp_err_to_name(we), we != ESP_OK ? "-" : (match ? "MATCH" : "*** MISMATCH ***"));
    }
  }

  uint8_t *big = (uint8_t *)heap_caps_malloc(64 * 1024, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (big) {
    memset(big, 0x3C, 64 * 1024);
    Serial.println("[raw] multi-block write sweep (no filesystem):");
    for (size_t blocks : {1u, 8u, 16u, 32u, 64u, 128u}) {
      const uint32_t t0 = millis();
      const esp_err_t we = sdmmc_write_sectors(&s_raw, big, scratch - 256, blocks);
      const uint32_t ms = millis() - t0;
      Serial.printf("[raw]   %4u blocks (%3u KB): %-16s %lu ms\n", (unsigned)blocks,
                    (unsigned)(blocks / 2), esp_err_to_name(we), (unsigned long)ms);
      if (we != ESP_OK) break;   // the card is wedged after a failure; later results are noise
    }
    free(big);
  }

#else
  Serial.println("[raw] raw-write tests SKIPPED (build with -DSD_ALLOW_RAW_WRITE to enable —");
  Serial.println("[raw]   they overwrite LBA 2048 and will destroy any filesystem on the card)");
#endif

  free(sec);
  sdmmc_host_deinit();   // hand the slot back before the VFS path re-initialises it
}

static void runSuite();

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== jukebox-sd: microSD bring-up + throughput ===");
  Serial.printf("[mem] internal free=%u KB   psram free=%u KB\n",
                (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024));

  // The I/O buffer goes in PSRAM: SOC_SDMMC_PSRAM_DMA_CAPABLE=y on the P4, so this is also a
  // check that DMA straight into PSRAM works — which is how the real art cache would read, into
  // the LVGL image buffer, with no bounce through internal RAM.
  s_buf = (uint8_t *)heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM);
  if (!s_buf) { Serial.println("[sd] FAIL: no PSRAM for the I/O buffer"); return; }
  memset(s_buf, 0xA5, CHUNK);

  // The internal-RAM counterpart, so "is it PSRAM DMA?" is answerable rather than assumed.
  s_int = (uint8_t *)heap_caps_malloc(CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!s_int) Serial.println("[sd] note: no internal DMA buffer — those trials will be skipped");
  else        memset(s_int, 0xA5, CHUNK);
  Serial.printf("[mem] buffers: psram=%p internal=%p (%u B each)\n", s_buf, s_int, (unsigned)CHUNK);

  runSuite();
}

// Everything after a successful mount. Called from setup() and, if the card wasn't ready then,
// again from loop() — so inserting a card later still produces a full result without a reflash.
static void runSuite() {
  const uint32_t heapBefore = ESP.getFreeHeap();
  probeRaw();   // identity + read/write split, below the filesystem — run this first, always
  if (!tryAllMounts()) { Serial.println("=== NO USABLE FILESYSTEM — will retry every 10s ==="); return; }
  Serial.printf("[mem] internal free after mount=%u KB (driver cost ~%u KB)\n",
                (unsigned)(ESP.getFreeHeap() / 1024),
                (unsigned)((heapBefore - ESP.getFreeHeap()) / 1024));

  // Raw sector writes are perfect at every size up to 64 KB (see the sweep in probeRaw) while any
  // fwrite past ~88 KB fails — so the fault is in the filesystem, not the card, the driver or the
  // bus. The filesystem is the one thing this probe never controlled: the card mounted on its own,
  // so format_if_mount_failed never fired and every write so far went into a FAT of unknown origin,
  // cluster size and cleanliness (and by now, unknown damage from the half-written files above).
  // Lay down a known-good FAT32 before believing any throughput number.
#ifdef SD_ALLOW_FORMAT
  static bool s_formatted = false;
  if (!s_formatted) {
    s_formatted = true;
    Serial.println("[sd] formatting FAT32 (SD_ALLOW_FORMAT) — this erases the card...");
    const uint32_t t0 = millis();
    const esp_err_t fe = esp_vfs_fat_sdcard_format(MOUNT, s_card);
    Serial.printf("[sd] format: %s (%lu ms)\n", esp_err_to_name(fe), (unsigned long)(millis() - t0));
    if (fe != ESP_OK) {
      // NOT fatal. The card already carries a mountable filesystem, and the benchmark — not the
      // formatter — is what this probe exists for. Aborting here is what hid the fact that the
      // sector-size fix had improved anything at all.
      Serial.println("[sd] format failed; continuing on the existing filesystem");
      unmountCard();
      if (!mountCard(false, SD_FREQ_KHZ)) { Serial.println("=== REMOUNT FAILED ==="); return; }
    }
  }
#endif

  if (!verifyRoundTrip()) { Serial.println("=== FAILED ==="); return; }

  // --- Why a matrix instead of just running the benchmark -------------------------------------
  // 4 KB writes succeed and 32 KB writes fail with sdmmc_wait_for_idle timeouts, and once one
  // fails every later file op returns I/O error — so a single sequential benchmark would report
  // one failure and tell us nothing about which variable caused it. Three suspects, each cheap:
  //   1. command_timeout_ms — the error IS a timeout, and a 64 GB card's multi-block write busy
  //      period can exceed the driver default.
  //   2. buffer memory — s_buf is PSRAM; SDMMC DMA out of PSRAM has alignment/cache rules that
  //      internal RAM does not.
  //   3. chunk size — 4 KB works, 32 KB doesn't; the boundary itself is informative.
  // Remount between trials: a failed write leaves the card and FATFS in a state where everything
  // afterwards fails, which would smear the first failure across every later result.
  struct Trial { const char *name; uint8_t **buf; size_t chunk; int timeoutMs; };
  // MEASURED so far: buffer memory and command timeout are both irrelevant — 32 KB fails from PSRAM
  // and from internal RAM, with the default timeout and with 5000 ms. 4 KB works (950 KB/s write,
  // 722 KB/s read). So sweep the size to find the actual boundary; the largest chunk that works is
  // what the real cache should use, and "4 KB because 32 KB broke once" is not an engineering
  // answer.
  // Re-run from scratch now that FatFs and the card agree on 512-byte sectors. Every earlier
  // result in this matrix was measured under the 4096-byte mismatch and is void — including the
  // "command timeout makes no difference" conclusion, which is why 5000 ms is back on the list.
  static const Trial trials[] = {
    {"psram     4K", &s_buf,  4 * 1024,    0},
    {"psram    32K", &s_buf, 32 * 1024,    0},
    {"psram    32K  timeout=5000ms", &s_buf, 32 * 1024, 5000},
    {"internal 32K  timeout=5000ms", &s_int, 32 * 1024, 5000},
  };

  Serial.println("\n=== write matrix: 156,800 B (one decoded 280x280 cover) ===");
  Serial.println("    the target to beat: 228 KB over the C6 link in 1353 ms");
  for (const Trial &t : trials) {
    if (!*t.buf) { Serial.printf("[trial] %s : buffer unavailable\n", t.name); continue; }
    Serial.printf("\n[trial] %s\n", t.name);
    unmountCard();
    s_cmdTimeoutMs = t.timeoutMs;
    if (!mountCard(false, SD_FREQ_KHZ)) { Serial.println("[trial]   remount failed, skipping"); continue; }
    timeWrite(MOUNT "/art.bin", ART_BLOB, *t.buf, t.chunk);
    timeRead(MOUNT "/art.bin", ART_BLOB, *t.buf, t.chunk);
    unlink(MOUNT "/art.bin");
  }

  // Only worth running once something above works; 2 MB through a broken config is just noise.
  // 4 KB is the size proven to work above, so the bulk figure is measured with it rather than with
  // a chunk that is already known to fail.
  Serial.println("\n=== 2 MB sequential @ 4K chunks ===");
  unmountCard();
  s_cmdTimeoutMs = 0;
  if (mountCard(false, SD_FREQ_KHZ)) {
    timeWrite(MOUNT "/bulk.bin", 2 * 1024 * 1024, s_buf, 4 * 1024);
    timeRead(MOUNT "/bulk.bin", 2 * 1024 * 1024, s_buf, 4 * 1024);
    unlink(MOUNT "/bulk.bin");
  }

  Serial.printf("\n[mem] internal free=%u KB   psram free=%u KB\n",
                (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024));
  Serial.println("=== done ===");
}

void loop() {
  // plans/07 lesson 2: boot prints race the serial reconnect, so a one-shot result at startup is
  // routinely missed and an empty capture window is indistinguishable from dead hardware. Retry
  // the WHOLE suite on a timer instead of latching a one-line summary — the first version of this
  // file printed "card NOT mounted" forever and withheld the one thing needed to fix it, the
  // esp_err from the mount. A probe that reprints its own diagnosis costs nothing and saves a
  // flash cycle per question. It also means a card inserted later just works.
  static uint32_t last = 0;
  if (!s_card && millis() - last > 10000) {
    last = millis();
    Serial.println("\n[sd] retrying...");
    runSuite();
  }
  delay(100);
}
