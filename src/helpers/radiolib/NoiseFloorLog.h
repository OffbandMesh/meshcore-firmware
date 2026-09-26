#pragma once
#include <stdint.h>

// #1275: when to put a noise-floor line on the wire.
//
// The sampler completes a sample set roughly twice a second, and it logged
// every one. Measured on rc32-bench-1 during #1076 Run A: 148 noise_floor lines
// in five minutes, against 10 `[pwr]` and 10 `[radio]`. About 90% of a tester's
// capture was this one line, burying everything the capture was collected to
// answer -- which is the whole point of #1053.
//
// Deleting it was the tempting fix, since the `[radio]` line added by #1071
// already reports `nf=` every 30 s. But `[radio]` is emitted only by the
// companion role, so deleting this would leave repeater and room_server diag
// builds with no noise-floor visibility at all. Hence rate-limiting: the value
// stays available on every role, at a rate a capture survives.
//
// Two ways a line is earned:
//   - the floor MOVED materially, which is the event worth seeing immediately;
//   - otherwise, a heartbeat no faster than the `[radio]` tick.
//
// Pure and Arduino-free: unit-tested natively in test/test_noise_floor_log.

namespace offband {
namespace noisefloor {

// Matches the `[pwr]`/`[radio]` cadence, so a diag capture carries one line per
// subsystem per tick rather than one subsystem drowning the rest.
constexpr uint32_t kMinLogIntervalMs = 30000;

// A real change in the RF environment, not sampling jitter. 3 dB is a doubling
// of noise power, which is the smallest change worth calling an event.
//
// Chosen by replaying all 91,361 samples the old code logged over 61 h of bench
// capture through this predicate `[verified: #1275 simulation, 2026-09-25]`:
//
//   threshold   lines/5 min   worst delay for a >=3 dB change
//   old code        124.6     --
//   3 dB             10.8     0.0 s      <- never delayed
//   6 dB              8.6     30.4 s
//
// 3 dB costs 2.2 lines per five minutes over a coarser threshold and removes
// the blind spot entirely: a real shift is always reported at once, never held
// for up to a tick. The volume is still cut by 91%.
constexpr int kSignificantMoveDb = 3;

// `logged_before` is false until the first line of this boot: the first floor
// the radio converges on is always worth stating, and it is what tells a reader
// the receiver came up at all.
inline bool shouldLog(int16_t floor_dbm, int16_t last_logged_dbm,
                      uint32_t now_ms, uint32_t last_log_ms, bool logged_before) {
  if (!logged_before) return true;

  int moved = (int)floor_dbm - (int)last_logged_dbm;
  if (moved < 0) moved = -moved;
  if (moved >= kSignificantMoveDb) return true;

  // Unsigned subtraction, so a millis() wrap yields the true elapsed time
  // rather than a huge value that would force a line, or a negative one that
  // would suppress lines for 49 days.
  return (now_ms - last_log_ms) >= kMinLogIntervalMs;
}

}  // namespace noisefloor
}  // namespace offband
