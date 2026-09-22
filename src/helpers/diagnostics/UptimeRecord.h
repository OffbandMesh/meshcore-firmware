#pragma once
#include <stdint.h>

// #1074: how long the previous boot ran, from two stores that cover
// different resets.
//
//  - RTC-retained memory, updated every second. Exact, costs no flash wear,
//    and survives task-watchdog, panic, software and USB-host resets (verified
//    on the RC32, see #754). Lost on power loss.
//  - NVS, which survives power loss, written on a schedule that front-loads
//    resolution where crash loops live and then stops: every kNvsFastMs until
//    kNvsFastUntilMs, every kNvsSlowMs until a save at or past
//    kNvsStopAfterMs, then never again that boot. About 37 writes per boot,
//    so flash wear no longer scales with uptime.
//
// At boot the RTC value is used when its record validates; otherwise the NVS
// value, which is flagged "at least" once it reached the stop point.
//
// Pure and Arduino-free: unit-tested natively in test/test_uptime_record.

namespace offband {
namespace uptime {

constexpr uint32_t kRtcPeriodMs    = 1000;
constexpr uint32_t kNvsFastMs      = 5000;
constexpr uint32_t kNvsFastUntilMs = 2u * 60u * 1000u;
constexpr uint32_t kNvsSlowMs      = 60000;
constexpr uint32_t kNvsStopAfterMs = 15u * 60u * 1000u;

// Retained across resets that keep RTC memory. `check` makes a record left by
// power-on noise, or torn by a reset between its word writes, fail validation
// rather than read as a plausible uptime.
//
// `boot` is the boot count of the boot that wrote it. A flash reset keeps RTC
// memory, so after build A -> build B (which never touches this record) -> a
// reset -> build A again, A would otherwise find its own record from two boots
// ago and report it as the previous boot. The next boot accepts the record only
// if `boot` is its own count minus one.
struct RetainedUptime {
  uint32_t magic;
  uint32_t boot;
  uint32_t uptime_s;
  uint32_t check;
};

constexpr uint32_t kRtcMagic = 0x0B0071A6u;

inline uint32_t checkFor(uint32_t boot, uint32_t uptime_s) {
  return ~(uptime_s ^ kRtcMagic) ^ (boot * 0x9E3779B1u);
}

inline void stamp(RetainedUptime& r, uint32_t boot, uint32_t uptime_s) {
  r.magic = kRtcMagic;
  r.boot = boot;
  r.uptime_s = uptime_s;
  r.check = checkFor(boot, uptime_s);
}

inline bool valid(const RetainedUptime& r) {
  return r.magic == kRtcMagic && r.check == checkFor(r.boot, r.uptime_s);
}

// Whether to refresh the RTC record now. `updated` is false until the first
// refresh of this boot.
inline bool rtcUpdateDue(uint32_t now_ms, uint32_t last_ms, bool updated) {
  return !updated || now_ms - last_ms >= kRtcPeriodMs;
}

// Whether an NVS save is due now. `saved` is false until the first save of
// this boot; `last_ms` is when that last save happened. Unsigned arithmetic,
// so a millis() wrap cannot restart the schedule once it has stopped.
inline bool nvsSaveDue(uint32_t now_ms, uint32_t last_ms, bool saved) {
  if (!saved) return now_ms >= kNvsFastMs;
  if (last_ms >= kNvsStopAfterMs) return false;
  const uint32_t interval = (now_ms < kNvsFastUntilMs) ? kNvsFastMs : kNvsSlowMs;
  return now_ms - last_ms >= interval;
}

// What the boot line reports for the previous boot.
struct Previous {
  uint32_t seconds;
  bool     from_rtc;   // false: from NVS
  bool     at_least;   // true: NVS had stopped saving, so it ran longer
};

// `this_boot` is the current boot's count; the RTC record counts only if the
// immediately previous boot wrote it.
inline Previous pickPrevious(const RetainedUptime& rtc, uint32_t this_boot, uint32_t nvs_s) {
  if (valid(rtc) && rtc.boot + 1u == this_boot) return Previous{rtc.uptime_s, true, false};
  return Previous{nvs_s, false, nvs_s >= kNvsStopAfterMs / 1000u};
}

}  // namespace uptime
}  // namespace offband
