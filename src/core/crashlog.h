// Read back the core dump the last panic left in flash, and make it readable REMOTELY.
//
// WHY THIS EXISTS. A panic prints its register dump and backtrace to the UART, and the jukebox is
// a wall panel whose rear port is power-only — so the one output that explains a crash is the one
// output that unit does not have. `health.resetReason` says 4 (ESP_RST_PANIC) and nothing more:
// you learn that it crashed, never where. That happened on 2026-08-27 and the crash could not be
// attributed at all.
//
// ESP-IDF already writes a full ELF core dump into a flash partition on panic
// (CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH, and the jukebox's partition table has the `coredump`
// partition it needs). Nothing ever read it back. This does, once at boot, and hands it to both
// LOG and /api/config.
//
// BOTH OUTPUTS ARE NEEDED, and that is the whole design:
//   * LOG alone is not enough. The mirror only delivers to clients CONNECTED AT THE TIME, and a
//     crash report printed at boot is printed precisely when nobody is attached — the device has
//     just restarted and any previous reader was disconnected by the reset.
//   * /api/config alone is not enough either: it is a pull, so it only helps someone who already
//     suspects a crash. The LOG line is what tells a watcher one happened.
// So: print it once, and keep it queryable for as long as the dump survives.
//
// THE DUMP IS DELIBERATELY NOT ERASED. It is the record of the last crash and costs nothing to
// keep; a later panic overwrites the partition anyway. Erasing after reporting would mean the one
// time you needed it — you were asleep when the LOG line went out — is the one time it is gone.
//
// RISC-V CANNOT BACKTRACE ON DEVICE (see esp_core_dump_summary_port.h): resolving return addresses
// needs DWARF, which is not on the chip. What you get instead is the crashing TASK NAME, the PC,
// the return address, and mcause/mtval — which is enough to name the fault and, with addr2line
// against the matching firmware.elf, the exact line. KEEP THAT ELF: `pio run` overwrites it, and
// without the build that crashed the addresses mean nothing.
//
// Compiles to nothing where the toolchain or partition table has no ELF core dump support, so it
// is safe in shared core — the two button units and the S3 boards just get an empty report.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace crashlog {

// Reads the stored dump exactly once. Cheap and safe to call before WiFi or the log mirror exist
// — it only touches flash — so call it early and print later.
void begin();

// True when the last boot was preceded by a panic whose dump is still readable.
bool have();

// One-line-per-field human summary. No-op when there is nothing to report.
void report(Print &out);

// Adds a `crash` object to the health JSON when there is one. Absent means "no stored crash",
// which is the normal, healthy case — the same convention psramMin uses.
void toJson(JsonObject health);

// --- Raw dump, for host-side unwinding --------------------------------------------------------
// The summary is not enough on RISC-V and that is not a shortcoming of this module: exc_pc for an
// assert points into panic_abort(), i.e. at the reporting machinery rather than at the code that
// failed, and the call chain that would name the real site cannot be walked on the chip. The whole
// ELF dump can be, on a host, by IDF's own tool:
//
//     curl -o dump.bin http://<device>/api/coredump
//     espcoredump.py --chip esp32p4 info_corefile -c dump.bin -t raw firmware.elf
//
// which prints the faulting backtrace AND every task's stack — the latter mattering because the
// task that takes a heap assert is whoever happened to call free() on a corrupted block, not
// necessarily whoever corrupted it.
//
// The firmware.elf must be the build that crashed; compare `elfSha` in health.crash. `pio run`
// overwrites firmware.elf, so keep a copy of any build you ship.
//
// Streamed in chunks rather than read whole: the partition is 64 KB and this device's internal
// heap is its scarce resource (CLAUDE.md — heapLargest has been as low as ~30 KB in normal
// running, so a 64 KB contiguous buffer is not merely wasteful, it would fail).

// --- Deliberate reboots -----------------------------------------------------------------------
// WHY THIS EXISTS, and it is a different hole from the core dump above. A panic leaves a dump; a
// DELIBERATE ESP.restart() leaves nothing at all. esp_reset_reason() reports 3 (ESP_RST_SW) for
// every one of them alike — the netTask stall reboot, the ESP-Hosted link recovery, a device-name
// change, a finished pull-OTA — so a rebooting device is indistinguishable from any other
// rebooting device.
//
// That is not hypothetical. Over 2026-09-01/02 this jukebox rebooted three times; three separate
// causes were proposed from the surrounding evidence and two were wrong, because every candidate
// path produces byte-identical symptoms: reset reason 3, no dump, ~2 min offline.
//
// THE LOG CANNOT COVER THIS, and that is the crux. Each reboot path does print its reason, but:
//   * the mirror only reaches clients connected AT THAT MOMENT, and
//   * netLinkRecover()'s reason travels over the very SDIO link whose death it is reporting, so
//     it can NEVER be delivered — no amount of flush or delay fixes a dead transport.
// A note in NVS survives the reset and is read back over a link that works again by then.
//
// READ AND CLEARED at boot, deliberately: the value then describes THIS boot. Left in place it
// would be read again after a power cut and blamed for a reboot it had nothing to do with — a
// stale explanation is worse than none, because it stops the search.
//
// Call IMMEDIATELY before ESP.restart(); Preferences commits on write, so it is durable even
// though the reset follows within milliseconds. Keep `reason` short and stable — it is a key to
// grep and compare across boots, not prose.
void noteReboot(const char *reason);

// Retract a note written speculatively. Needed by the pull-OTA path, which must write its note
// BEFORE calling into HTTPUpdate (a successful update reboots inside the library and never
// returns) and therefore has to undo it on the paths that do return. Without this, a failed
// update would leave "otapull" behind to be blamed for whatever caused the next reboot.
void clearRebootNote();

// The reason recorded before the reset that started this boot, or "" when there was none —
// which is itself informative: it means an UNPLANNED reset (panic, brownout, power cut, or the
// hardware watchdog), so cross-read it with health.resetReason.
const char *lastReboot();

// Size in bytes of the stored dump, or 0 when there is none to serve.
size_t dumpSize();

// Copies up to `len` bytes at `offset` into `buf`. Returns bytes copied; 0 at or past the end.
size_t dumpRead(size_t offset, uint8_t *buf, size_t len);

}  // namespace crashlog
