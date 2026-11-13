#pragma once

// #1626: a running low-battery cutoff for a board with no display, which hands off to
// SafeBoot instead of ending the board.
//
// The existing cutoff lives in the companion's UI tasks, so a repeater or a headless
// companion has none: SafeBoot only checks at boot, and a running node on an unprotected
// cell draws it flat. And that cutoff calls powerOff(), which arms no wake source, so a
// solar node that trips it would stay off until someone visited it. This one restarts into
// SafeBoot's sleep, which owns the recovery: "runtime shutdown decides when to stop;
// SafeBoot decides when to start" (#1007).
//
// The decision is lowBattHandoffShouldFire(): the same debounced rule the UI cutoff uses,
// plus SafeBoot's floor for what counts as a reading (#2118), so the two never disagree and
// loop. test_low_batt_cutoff drives both. Opt-in per variant with OFFBAND_LOW_BATT_HANDOFF.

#if defined(OFFBAND_LOW_BATT_HANDOFF)

#if !defined(AUTO_SHUTDOWN_MILLIVOLTS)
  #error "OFFBAND_LOW_BATT_HANDOFF needs AUTO_SHUTDOWN_MILLIVOLTS"
#endif
#if !defined(NRF52_PLATFORM)
  #error "OFFBAND_LOW_BATT_HANDOFF needs SafeBoot's nRF52 battery wake"
#endif
#if !defined(BATTERY_LPCOMP_INPUT)
  #error "OFFBAND_LOW_BATT_HANDOFF without BATTERY_LPCOMP_INPUT would never wake on a recovering cell"
#endif
#if defined(DISPLAY_CLASS)
  #error "OFFBAND_LOW_BATT_HANDOFF is for boards with no display; the UI task runs its own cutoff"
#endif

#include <Arduino.h>
#include <SafeBoot.h>
#include <MeshLog.h>
#include <helpers/ui/BatteryGauge.h>
#include <helpers/ui/LowBattCutoff.h>

namespace offband {

template <class Board>
inline void lowBattHandoffTick(Board& board) {
  static LowBattCutoffState state;
  static BatteryAverage average;
  static uint32_t next_check_ms = 0;

  const uint32_t now_ms = millis();
  if ((int32_t)(now_ms - next_check_ms) < 0) return;
  next_check_ms = now_ms + kLowBattCheckIntervalMs;

  const uint16_t raw_mv = board.getBattMilliVolts();
  average.feed(raw_mv);
  const uint16_t smoothed_mv = average.seeded() ? average.value() : raw_mv;
  const bool external = board.isExternalPowered();
  const bool fire = lowBattHandoffShouldFire(state, now_ms, raw_mv, smoothed_mv,
                                             (uint16_t)AUTO_SHUTDOWN_MILLIVOLTS,
                                             OFFBAND_LOW_BATT_SUSTAIN_MS, external);

  mesh_log_line(MLOG_DEBUG, "[batt] raw=%u sm=%u thr=%u ext=%d low_ms=%lu fire=%d\r\n",
                (unsigned)raw_mv, (unsigned)smoothed_mv, (unsigned)AUTO_SHUTDOWN_MILLIVOLTS,
                external ? 1 : 0, (unsigned long)lowBattHeldMs(state, now_ms), fire ? 1 : 0);

  if (fire) {
    mesh_log_print(MLOG_BOOT, "[batt] under %u mV for %lu ms: restarting into SafeBoot's sleep\r\n",
                   (unsigned)AUTO_SHUTDOWN_MILLIVOLTS, (unsigned long)OFFBAND_LOW_BATT_SUSTAIN_MS);
    SafeBoot::restartIntoSleep();
  }
}

}  // namespace offband

#endif  // OFFBAND_LOW_BATT_HANDOFF
