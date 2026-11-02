#include "StallWatch.h"

#if defined(OFFBAND_STALL_WATCH) && defined(NRF52_PLATFORM)

#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>
#include "CrashLog.h"
#include "StallRules.h"

namespace offband {
namespace {

TaskHandle_t s_loop = nullptr;
volatile uint32_t s_fed_ms = 0;
volatile uint8_t s_sent = 0;
SoftwareTimer s_timer;

// Runs on the FreeRTOS timer task, which outranks the loop task, so it runs while the loop
// is stuck. If no [stall] line appears before a watchdog reset, the stall is somewhere that
// stops this too: an interrupt handler, interrupts disabled, or the scheduler suspended.
void check(TimerHandle_t) {
  if (s_loop == nullptr) return;
  const uint32_t stalled = millis() - s_fed_ms;
  if (!stallReportDue(stalled, s_sent)) return;
  s_sent = s_sent + 1;

  // The first word of a FreeRTOS TCB is pxTopOfStack: where the loop task's saved context
  // sits while the timer task is running.
  const uint32_t* top = *reinterpret_cast<uint32_t* const*>(s_loop);
  // A stack overflow can leave this pointer anywhere, and reading through it would fault
  // inside the diagnostic. Keep every read below inside the nRF52840's 256 KB of RAM.
  const uintptr_t t = reinterpret_cast<uintptr_t>(top);
  if (t < 0x20000000u || t + 41u * 4u > 0x20040000u || (t & 3u) != 0) {
    crashLogf("[stall] #%u loop stuck %lu ms: saved stack pointer 0x%08lx is not in RAM",
              (unsigned)s_sent, (unsigned long)stalled, (unsigned long)t);
    return;
  }
  const uint32_t* hw = top + stallHwFrameWords(top[8]);
  crashLogf("[stall] #%u loop stuck %lu ms: pc=0x%08lx lr=0x%08lx state=%d", (unsigned)s_sent,
            (unsigned long)stalled, (unsigned long)hw[kStallFramePc],
            (unsigned long)hw[kStallFrameLr], (int)eTaskGetState(s_loop));
  // The words above the frame, for a manual unwind: return addresses into the firmware
  // stand out against data.
  const uint32_t* sp = hw + 8;
  crashLogf("[stall] #%u stack %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx", (unsigned)s_sent,
            (unsigned long)sp[0], (unsigned long)sp[1], (unsigned long)sp[2], (unsigned long)sp[3],
            (unsigned long)sp[4], (unsigned long)sp[5], (unsigned long)sp[6], (unsigned long)sp[7]);
}

}  // namespace

void stallWatchBegin() {
  s_loop = xTaskGetCurrentTaskHandle();
  s_fed_ms = millis();
  s_timer.begin(1000, check);
  s_timer.start();
  crashLogf("[stall] watch armed: reports at 15/20/25 s without a loop feed");
}

void stallWatchFeed() {
  s_fed_ms = millis();
  s_sent = 0;
}

}  // namespace offband

#endif
