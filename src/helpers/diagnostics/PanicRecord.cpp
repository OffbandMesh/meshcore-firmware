#include "PanicRecord.h"

#if defined(ESP32) && defined(OFFBAND_PANIC_RECORD) && defined(__XTENSA__)

#include <stdint.h>
#include <string.h>

#include <esp_attr.h>
#include <esp_debug_helpers.h>
#include <esp_private/panic_internal.h>
#include <xtensa/xtensa_context.h>

#include <MeshLog.h>

namespace {

constexpr uint32_t kMagic = 0x50414E43;   // "PANC"
constexpr int kFrames = 16;

struct Record {
  uint32_t magic;
  int32_t core;
  uint32_t pc, cause, vaddr;
  uint32_t frames;
  uint32_t bt[kFrames];
  char reason[40];
};

// RTC memory: kept through the panic's reset, lost on power-off.
RTC_NOINIT_ATTR Record s_rec;

}  // namespace

extern "C" void __real_esp_panic_handler(panic_info_t* info);

// Runs inside the panic, before ESP-IDF's handler prints and resets. Only
// plain stores and the stack walk the handler itself uses; nothing that
// allocates, locks or logs.
extern "C" void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t* info) {
  if (info) {
    s_rec.core = info->core;
    s_rec.pc = uint32_t(info->addr);
    s_rec.cause = 0;
    s_rec.vaddr = 0;
    s_rec.frames = 0;
    s_rec.reason[0] = 0;
    if (info->reason) {
      size_t i = 0;
      for (; i + 1 < sizeof s_rec.reason && info->reason[i]; i++) s_rec.reason[i] = info->reason[i];
      s_rec.reason[i] = 0;
    }
    const XtExcFrame* f = static_cast<const XtExcFrame*>(info->frame);
    if (f) {
      s_rec.cause = uint32_t(f->exccause);
      s_rec.vaddr = uint32_t(f->excvaddr);
      esp_backtrace_frame_t bt = {};
      bt.pc = uint32_t(f->pc);
      bt.sp = uint32_t(f->a1);
      bt.next_pc = uint32_t(f->a0);
      bt.exc_frame = f;
      s_rec.bt[s_rec.frames++] = bt.pc;
      while (s_rec.frames < uint32_t(kFrames) && bt.next_pc && esp_backtrace_get_next_frame(&bt)) {
        s_rec.bt[s_rec.frames++] = bt.pc;
      }
    }
    if (s_rec.frames == 0) s_rec.bt[s_rec.frames++] = s_rec.pc;   // no frame: the fault address alone
    s_rec.magic = kMagic;
  }
  __real_esp_panic_handler(info);
}

namespace offband {

void panicRecordReport() {
  const bool plausible = s_rec.magic == kMagic && (s_rec.core == 0 || s_rec.core == 1) &&
                         s_rec.frames >= 1 && s_rec.frames <= uint32_t(kFrames);
  s_rec.magic = 0;   // once: a clean reboot after this reports nothing
  // RTC memory holds whatever it powered up with; a record that can't be
  // one isn't reported as a crash.
  if (!plausible) return;
  char bt[kFrames * 11 + 1];
  size_t o = 0;
  const uint32_t n = s_rec.frames <= uint32_t(kFrames) ? s_rec.frames : uint32_t(kFrames);
  for (uint32_t i = 0; i < n && o + 12 < sizeof bt; i++) {
    // A return address carries the call's window in its top bits: put the
    // code region's back, as ESP-IDF's backtrace printer does.
    const uint32_t pc = (s_rec.bt[i] & 0x3FFFFFFF) | 0x40000000;
    static const char kHex[] = "0123456789abcdef";
    bt[o++] = ' ';
    bt[o++] = '0';
    bt[o++] = 'x';
    for (int s = 28; s >= 0; s -= 4) bt[o++] = kHex[(pc >> s) & 0xF];
  }
  bt[o] = 0;
  s_rec.reason[sizeof s_rec.reason - 1] = 0;
  mesh_log_line(MLOG_BOOT, "[panic] core=%d reason=%s pc=0x%08lx cause=%lu vaddr=0x%08lx bt:%s\n", int(s_rec.core),
                s_rec.reason, (unsigned long)s_rec.pc, (unsigned long)s_rec.cause, (unsigned long)s_rec.vaddr, bt);
}

}  // namespace offband

#else

namespace offband {
void panicRecordReport() {}
}  // namespace offband

#endif
