#pragma once

// #1667: when the low-battery cutoff may shut the board down, as a pure rule.
//
// WHAT WENT WRONG. The cutoff used to act on ONE raw ADC sample:
//
//     uint16_t milliVolts = getBattMilliVolts();
//     if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS) { shutdown(...); }
//
// No averaging, no debounce, nothing requiring the reading to repeat. The owner's badge
// shut down with its gauge showing ~80%, and an independent MAX17048 + INA228 on the
// bench rig agreed with the gauge to within 6 mV at 4026 mV. A cutoff at 3400 mV from
// there is a ~620 mV excursion, which at the badge's ~120 mA TX draw would need about
// 5 ohms of series resistance -- that is not a battery, so it was a bad read.
//
// The asymmetry was already documented in the code it lived next to, ui-new/UITask.h:
// "the battery as the screens should show it -- an average across readings rather than
// whatever the ADC said at the instant of a redraw. getBattMilliVolts() is still the raw
// read, and is what the low-battery shutdown acts on." The display got an average in
// #1246. The thing that powers the board off did not.
//
// WHY A PURE HEADER. The old rule lived in two `.cpp` files the native envs do not
// compile -- ui-new and ui-tiny, with the same defect and no shared code -- so no test
// could reach it. One rule here, both callers thin, and test_low_batt_cutoff drives every
// branch including the one that matters: raw low while smoothed is healthy must NOT fire.
//
// THE DESTRUCTIVE DECISION SHOULD HAVE THE STRONGER EVIDENCE. SafeBoot already demanded
// 8 consecutive samples above its wake threshold before making the *reversible* choice to
// continue booting. The *irreversible* choice took one sample. That is backwards, and it
// is the shape of the bug rather than an incidental detail.

#include <stdint.h>

// How long the low condition must hold continuously before the board powers off.
// The owner's figure, 2026-10-19: "at least 300s consistent below minimum."
//
// ZERO DISABLES THE CUTOFF ENTIRELY. That is deliberate and it is what a burndown image
// sets, so the board runs until it genuinely browns out and the real discharge curve can
// be measured instead of argued about.
#ifndef OFFBAND_LOW_BATT_SUSTAIN_MS
  #define OFFBAND_LOW_BATT_SUSTAIN_MS 300000UL
#endif

namespace offband {

// How long a check cadence is assumed to be. Only used to size the sanity assertion
// below; the rule itself is time-based and does not care how often it is called.
constexpr uint32_t kLowBattCheckIntervalMs = 8000;

static_assert(OFFBAND_LOW_BATT_SUSTAIN_MS == 0 ||
              OFFBAND_LOW_BATT_SUSTAIN_MS >= 4 * kLowBattCheckIntervalMs,
              "a sustain window shorter than a few check intervals is not a debounce -- "
              "set 0 to disable the cutoff outright instead of making it twitchy");

// The caller's running state. One instance per UI task; zero-initialised is correct.
struct LowBattCutoffState {
  uint32_t below_since_ms;   // when the low condition first became true
  bool below;                // whether it is currently true
};

// Is this pair of readings low enough to count at all?
//
// BOTH must be below the threshold. A zero means the sensor is not ready or there is no
// battery rail -- USB-powered boards read that way -- and must never count as low.
//
// Requiring the smoothed value too is what makes a single outlier harmless: it has to
// move the average before it can contribute, and one sample out of an average cannot.
inline bool lowBattReadingIsLow(uint16_t raw_mv, uint16_t smoothed_mv,
                                uint16_t threshold_mv) {
  if (threshold_mv == 0) return false;
  if (raw_mv == 0 || smoothed_mv == 0) return false;
  return raw_mv < threshold_mv && smoothed_mv < threshold_mv;
}

// Fold one reading into the state and say whether the board should now power off.
//
// `now_ms` is millis(). Elapsed time is computed by unsigned subtraction, which wraps
// correctly across the 32-bit rollover at ~49.7 days -- so a cutoff cannot fire early
// because the counter wrapped mid-window, and cannot be delayed by it either.
inline bool lowBattShouldShutdown(LowBattCutoffState& st, uint32_t now_ms,
                                  uint16_t raw_mv, uint16_t smoothed_mv,
                                  uint16_t threshold_mv, uint32_t sustain_ms,
                                  bool external_powered) {
  // External power is not a low battery, whatever the rail reads. Reset rather than
  // pause: plugging in should clear the history, not bank it for the next unplug.
  if (external_powered) {
    st.below = false;
    st.below_since_ms = 0;
    return false;
  }

  if (!lowBattReadingIsLow(raw_mv, smoothed_mv, threshold_mv)) {
    st.below = false;
    st.below_since_ms = 0;
    return false;
  }

  if (!st.below) {
    st.below = true;
    st.below_since_ms = now_ms;
  }

  // Disabled. The state above is still tracked, so a burndown image's log still shows
  // how long the board has been under the line -- it just never acts on it.
  if (sustain_ms == 0) return false;

  return (uint32_t)(now_ms - st.below_since_ms) >= sustain_ms;
}

// How long the low condition has held, for a log line. Zero when it is not holding.
inline uint32_t lowBattHeldMs(const LowBattCutoffState& st, uint32_t now_ms) {
  if (!st.below) return 0;
  return (uint32_t)(now_ms - st.below_since_ms);
}

}  // namespace offband
