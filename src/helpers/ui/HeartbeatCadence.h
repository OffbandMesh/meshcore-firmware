#pragma once

// How often the #275 status-LED heartbeat blinks, and why it is a per-variant number.
//
// WHAT THE HEARTBEAT IS FOR. It is a liveness indicator, not decoration: the blip is
// driven from the main loop, so a pattern that stops means the loop wedged. That is the
// whole property, and it is why the paired badge blinks rather than going dark.
//
// WHY THE DEFAULT IS 1 Hz. It was sized for the solar car companion -- an unattended,
// sealed node with NO SCREEN, where a 1 Hz blip at ~10% duty is the only way to tell it is
// alive and the power budget is the binding constraint.
//
// WHY A BADGE WANTS SOMETHING ELSE. The badge inherited that default onto three bright
// LEDs worn on a person's chest, and the owner's word for it on 2026-10-18 was
// "obnoxious". A badge also HAS a screen, so the liveness argument is weaker: a wedged
// loop freezes the display long before anyone notices a missing blip. So the badge slows
// its own cadence rather than the shared default changing under the board it was written
// for.
//
// THE ROUNDING IS THE SUBTLE PART, and it is what this header exists to make testable.
// The loop wakes on a fixed timer (100 ms today) and the toggle fires at the first wake AT
// OR AFTER the deadline, so every requested interval is rounded UP to a wake boundary. The
// shipped values are deliberately a hair UNDER a boundary -- 90 under 100, 890 under 900 --
// so the edge lands on a known wake instead of racing one. A value sitting exactly ON a
// boundary is the trap: 4900 against a 100 ms wake can resolve to 4900 or 5000 depending
// on jitter, where 4890 always resolves to 4900. A session proposed 4900 for exactly this
// reason and was wrong; see the tests.
//
// Pure: no Arduino, no board. Host-tested in test/test_heartbeat_cadence.

#include <stdint.h>

namespace offband {

// The loop's wake period. Everything below rounds up to a multiple of this.
constexpr uint32_t kHeartbeatWakeMs = 100;

// What a request actually becomes once the wake timer has its say: the first wake at or
// after the deadline. A request that is already a multiple lands on itself.
inline uint32_t heartbeatObservedMs(uint32_t requested_ms, uint32_t wake_ms = kHeartbeatWakeMs) {
  if (wake_ms == 0) return requested_ms;
  const uint32_t rem = requested_ms % wake_ms;
  return rem == 0 ? requested_ms : requested_ms + (wake_ms - rem);
}

// Lit time + dark time, as observed. This is the number a person sees as "the rate".
inline uint32_t heartbeatPeriodMs(uint32_t on_ms, uint32_t off_ms,
                                  uint32_t wake_ms = kHeartbeatWakeMs) {
  return heartbeatObservedMs(on_ms, wake_ms) + heartbeatObservedMs(off_ms, wake_ms);
}

// Does this value sit safely under a wake boundary, rather than exactly on one?
//
// Exactly-on is not a correctness bug -- the LED still blinks -- but it makes the observed
// interval depend on whether the comparison runs a microsecond early or late, which is the
// one thing a liveness indicator should not be vague about.
inline bool heartbeatValueIsClearOfBoundary(uint32_t ms, uint32_t wake_ms = kHeartbeatWakeMs) {
  return wake_ms != 0 && ms != 0 && (ms % wake_ms) != 0;
}

}  // namespace offband
