#pragma once

// #1966: the pure parts of the nRF52 stall watch, so they are host-tested.
//
// The bootloader wipes RAM before the app runs (#378), so nothing in memory survives a
// watchdog reset on these boards. The stall watch reports BEFORE the 30 s watchdog fires,
// from a FreeRTOS timer that keeps running while the loop task is stuck.

#include <stdint.h>

namespace offband {

// When to report: at 15, 20 and 25 s without a feed, so three readings land before the
// 30 s watchdog. A PC that moves between them means the loop is busy somewhere; one that
// stays put means it is blocked or spinning on one line.
constexpr uint32_t kStallFirstReportMs = 15000;
constexpr uint32_t kStallReportGapMs = 5000;
constexpr uint8_t kStallMaxReports = 3;

// Whether report number `sent` (0-based) is due, `stalled_ms` after the last feed.
inline bool stallReportDue(uint32_t stalled_ms, uint8_t sent) {
  if (sent >= kStallMaxReports) return false;
  return stalled_ms >= kStallFirstReportMs + (uint32_t)sent * kStallReportGapMs;
}

// Where the hardware-stacked frame (R0 R1 R2 R3 R12 LR PC xPSR) starts in a switched-out
// task's saved stack, in 32-bit words from its top. The FreeRTOS Cortex-M4F port saves
// R4-R11 and EXC_RETURN first (9 words), and S16-S31 (16 more) only when bit 4 of
// EXC_RETURN is clear, meaning the task used the FPU.
inline uint32_t stallHwFrameWords(uint32_t exc_return) {
  return (exc_return & 0x10u) ? 9u : 25u;
}

constexpr uint32_t kStallFrameLr = 5;   // within the hardware frame
constexpr uint32_t kStallFramePc = 6;

}  // namespace offband
