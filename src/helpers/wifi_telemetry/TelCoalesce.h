#pragma once
// #2232: one Wi-Fi session per telemetry cycle.
//
// The publish and the HTTP command poll run on the same default 5-minute
// interval, each scheduled from its own completion, so they come due a few
// seconds apart. Each used to bring Wi-Fi up, do its work and tear it down, so
// every cycle joined twice and the first session lasted under half a second.
// R2-SAW crashed twice inside such a session (esp_netif_down -> tcp_abort ->
// low_level_output on a station that was leaving).
//
// The rule: a trigger that finishes keeps the link up when the other trigger is
// running or due within kTelCoalesceWindowMs, and the other trigger then uses
// that session and tears down after itself. Teardown is delayed by at most the
// window, and only when the other trigger will run inside it, so burst mode
// still ends every cycle with Wi-Fi off.
//
// Pure: no Arduino, host-tested in test/test_tel_coalesce.

#include <stdint.h>

namespace offband {

constexpr uint32_t kTelCoalesceWindowMs = 30000;

// True when the link must stay up for the other trigger: it is mid-cycle, or
// its due time (millis, wrap-safe) is past or within `window_ms` of `now`.
// `other_enabled` false (trigger not built in, or not scheduled) never holds.
inline bool telHoldLinkForOther(uint32_t now, bool other_enabled, bool other_active,
                                uint32_t other_due_ms,
                                uint32_t window_ms = kTelCoalesceWindowMs) {
  if (!other_enabled) return false;
  if (other_active) return true;
  const int32_t until_due = (int32_t)(other_due_ms - now);
  return until_due <= (int32_t)window_ms;
}

}  // namespace offband
