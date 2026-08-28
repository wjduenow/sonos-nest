#include "crashlog.h"
#include "core/net/logmirror.h"   // LOG — tees to the TCP mirror where enabled

#include <esp_core_dump.h>
#include <esp_system.h>
#include <esp_idf_version.h>
#include <esp_partition.h>

// The summary API only exists when the dump is an ELF written to flash. Everything below collapses
// to no-ops otherwise, so this file is safe in shared core: a board whose partition table has no
// coredump partition just reports nothing.
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) && defined(CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF)
  #define CRASHLOG_SUPPORTED 1
#else
  #define CRASHLOG_SUPPORTED 0
#endif

// THE EXTRA-INFO STRUCT IS ARCHITECTURE-SPECIFIC, AND THE TWO HAVE NOTHING IN COMMON. This is not
// a detail to paper over with a lowest-common-denominator readout — the useful fields differ:
//
//   RISC-V (ESP32-P4, the jukebox):  mcause / mtval / ra / sp, and NO on-device backtrace at all.
//       Resolving a call chain needs DWARF, which is not on the chip; IDF gives a raw stack dump
//       to unwind on the host instead.
//   Xtensa (ESP32-S3, every other unit):  exc_cause / exc_vaddr, and a REAL backtrace (bt[],
//       depth, corrupted) — the windowed-register ABI lets it walk the stack on device.
//
// Written arch-first for that reason: the Xtensa side would be strictly worse if it were forced
// through a shape chosen for RISC-V, since it would throw away the backtrace. Compiler predefines
// rather than CONFIG_IDF_TARGET_ARCH_*, because those are not spelled the same across the two
// framework versions this repo builds against (Arduino 2.0.17 for the S3 units, 3.x for the P4).
#if defined(__riscv)
  #define CRASHLOG_RISCV 1
#else
  #define CRASHLOG_RISCV 0
#endif

// esp_core_dump_get_panic_reason() is an IDF 5.x addition. The S3 units build against Arduino
// 2.0.17 / IDF 4.4, where the rest of the summary API exists but this one call does not — so it
// is version-gated rather than arch-gated. Losing it costs only the human-readable reason string;
// the task name, PC and cause registers all still come through.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  #define CRASHLOG_HAS_PANIC_REASON 1
#else
  #define CRASHLOG_HAS_PANIC_REASON 0
#endif

namespace crashlog {
namespace {

bool s_read = false;     // begin() has run
bool s_have = false;     // ...and found a dump

#if CRASHLOG_SUPPORTED
esp_core_dump_summary_t s_sum;
char s_panicReason[128] = {0};

#if CRASHLOG_RISCV
// RISC-V trap cause. Naming these turns "mcause=5" into a diagnosis without reaching for the
// manual: on this project a load/store access fault is almost always a null or freed pointer, and
// an illegal instruction is usually a corrupted stack or a call through a bad function pointer.
const char *causeName(uint32_t c) {
  switch (c) {
    case 0:  return "instruction address misaligned";
    case 1:  return "instruction access fault";
    case 2:  return "illegal instruction";
    case 3:  return "breakpoint";
    case 4:  return "load address misaligned";
    case 5:  return "load access fault";
    case 6:  return "store address misaligned";
    case 7:  return "store access fault";
    case 8:  return "ecall from U-mode";
    case 11: return "ecall from M-mode";
    default: return "";
  }
}
#else
// Xtensa EXCCAUSE. 28/29 are the classic null/bad-pointer dereferences and are what you expect to
// see for the ordinary crash; 0 and 20 usually mean the stack or a function pointer is corrupt.
const char *causeName(uint32_t c) {
  switch (c) {
    case 0:  return "illegal instruction";
    case 2:  return "instruction fetch error";
    case 3:  return "load/store error";
    case 6:  return "integer divide by zero";
    case 9:  return "load/store alignment";
    case 20: return "instruction fetch prohibited";
    case 28: return "load prohibited";
    case 29: return "store prohibited";
    default: return "";
  }
}
#endif  // CRASHLOG_RISCV
#endif  // CRASHLOG_SUPPORTED

}  // namespace

void begin() {
  if (s_read) return;
  s_read = true;
#if CRASHLOG_SUPPORTED
  // image_check() validates the CRC before we trust any of it. A partition never written, or a
  // dump truncated by a reset mid-write, both land here — and printing garbage registers as if
  // they were a crash is worse than printing nothing.
  if (esp_core_dump_image_check() != ESP_OK) return;
  if (esp_core_dump_get_summary(&s_sum) != ESP_OK) return;
  s_have = true;
#if CRASHLOG_HAS_PANIC_REASON
  if (esp_core_dump_get_panic_reason(s_panicReason, sizeof(s_panicReason)) != ESP_OK)
    s_panicReason[0] = 0;
#endif
#endif
}

bool have() {
  return s_have;
}

void report(Print &out) {
#if CRASHLOG_SUPPORTED
  if (!s_have) return;
  out.printf("[crash ] STORED CORE DUMP from a previous panic (reset reason now %d)\n",
             (int)esp_reset_reason());
  if (s_panicReason[0]) out.printf("[crash ]   reason : %s\n", s_panicReason);
  out.printf("[crash ]   task   : %.16s\n", s_sum.exc_task);
  out.printf("[crash ]   pc     : 0x%08lx\n", (unsigned long)s_sum.exc_pc);

#if CRASHLOG_RISCV
  const char *cn = causeName(s_sum.ex_info.mcause);
  out.printf("[crash ]   mcause : %lu%s%s%s   mtval: 0x%08lx\n",
             (unsigned long)s_sum.ex_info.mcause, cn[0] ? " (" : "", cn, cn[0] ? ")" : "",
             (unsigned long)s_sum.ex_info.mtval);
  out.printf("[crash ]   ra     : 0x%08lx   sp: 0x%08lx\n",
             (unsigned long)s_sum.ex_info.ra, (unsigned long)s_sum.ex_info.sp);
  // No on-device backtrace on RISC-V — pc and ra are the two addresses worth resolving.
  out.printf("[crash ]   decode : riscv32-esp-elf-addr2line -pfiaC -e firmware.elf 0x%08lx 0x%08lx\n",
             (unsigned long)s_sum.exc_pc, (unsigned long)s_sum.ex_info.ra);
#else
  const char *cn = causeName(s_sum.ex_info.exc_cause);
  out.printf("[crash ]   cause  : %lu%s%s%s   vaddr: 0x%08lx\n",
             (unsigned long)s_sum.ex_info.exc_cause, cn[0] ? " (" : "", cn, cn[0] ? ")" : "",
             (unsigned long)s_sum.ex_info.exc_vaddr);
  // Xtensa walks its own stack. Print the whole chain on one line so it can be pasted straight
  // into addr2line, and say so when it is untrustworthy rather than letting it read as fact.
  out.printf("[crash ]   decode : xtensa-esp32s3-elf-addr2line -pfiaC -e firmware.elf");
  for (uint32_t i = 0; i < s_sum.exc_bt_info.depth && i < 16; i++)
    out.printf(" 0x%08lx", (unsigned long)s_sum.exc_bt_info.bt[i]);
  out.printf("%s\n", s_sum.exc_bt_info.corrupted ? "   (BACKTRACE CORRUPT — treat as a hint)" : "");
#endif

  out.printf("[crash ]   elfsha : %s\n", (const char *)s_sum.app_elf_sha256);
  // Said explicitly because it is the step that is easy to lose: `pio run` overwrites
  // firmware.elf, and once the build that crashed is gone the addresses above mean nothing.
  out.println("[crash ]   NOTE: addresses resolve only against the ELF of the build that crashed.");
#else
  (void)out;
#endif
}

void toJson(JsonObject health) {
#if CRASHLOG_SUPPORTED
  if (!s_have) return;                 // absent == no stored crash, the healthy case
  JsonObject c = health["crash"].to<JsonObject>();
  if (s_panicReason[0]) c["reason"] = s_panicReason;
  c["task"]   = (const char *)s_sum.exc_task;
  c["pc"]     = s_sum.exc_pc;
  c["elfSha"] = (const char *)s_sum.app_elf_sha256;
#if CRASHLOG_RISCV
  c["arch"]   = "riscv";
  c["mcause"] = s_sum.ex_info.mcause;
  c["mtval"]  = s_sum.ex_info.mtval;
  c["ra"]     = s_sum.ex_info.ra;
  c["sp"]     = s_sum.ex_info.sp;
  const char *cn = causeName(s_sum.ex_info.mcause);
#else
  c["arch"]     = "xtensa";
  c["excCause"] = s_sum.ex_info.exc_cause;
  c["excVaddr"] = s_sum.ex_info.exc_vaddr;
  JsonArray bt = c["bt"].to<JsonArray>();
  for (uint32_t i = 0; i < s_sum.exc_bt_info.depth && i < 16; i++) bt.add(s_sum.exc_bt_info.bt[i]);
  if (s_sum.exc_bt_info.corrupted) c["btCorrupt"] = true;
  const char *cn = causeName(s_sum.ex_info.exc_cause);
#endif
  if (cn[0]) c["causeName"] = cn;
  const size_t n = dumpSize();
  if (n) c["dumpBytes"] = (uint32_t)n;   // tells a reader /api/coredump has something to give
#else
  (void)health;
#endif
}

// --- Raw dump ---------------------------------------------------------------------------------
// Deliberately NOT gated on CRASHLOG_SUPPORTED: image_check()/image_get() exist wherever the
// coredump component does, even on the IDF 4.4 boards whose summary API is thinner. A board with
// no coredump partition simply finds nothing and serves 404.

size_t dumpSize() {
  size_t addr = 0, size = 0;
  if (esp_core_dump_image_check() != ESP_OK) return 0;
  if (esp_core_dump_image_get(&addr, &size) != ESP_OK) return 0;
  return size;
}

size_t dumpRead(size_t offset, uint8_t *buf, size_t len) {
  size_t addr = 0, size = 0;
  if (!buf || !len) return 0;
  if (esp_core_dump_image_check() != ESP_OK) return 0;
  if (esp_core_dump_image_get(&addr, &size) != ESP_OK) return 0;
  if (offset >= size) return 0;
  if (offset + len > size) len = size - offset;

  // image_get() returns an address in the flash map; esp_partition_read wants an offset WITHIN the
  // partition, so it has to be rebased. Reading through the partition API rather than raw flash
  // keeps the bounds check that stops a bad offset walking into the app image.
  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
  if (!part) return 0;
  if (addr < part->address) return 0;
  const size_t within = addr - part->address;
  if (within + offset + len > part->size) return 0;
  if (esp_partition_read(part, within + offset, buf, len) != ESP_OK) return 0;
  return len;
}

}  // namespace crashlog
