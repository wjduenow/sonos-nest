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

}  // namespace crashlog
