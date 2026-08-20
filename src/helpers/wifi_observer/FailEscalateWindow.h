#pragma once
#include <cstdint>

// Dependency-free, host-testable dedupe for broker failure-penalty escalation
// (#838 / #906). A single failed connection attempt can surface as up to three
// esp-mqtt events within milliseconds -- ERROR alone (connect refused /
// unreachable, no DISCONNECTED follows), ERROR+DISCONNECTED (TLS/auth failure
// after the TCP connect), or DISCONNECTED alone (clean drop). We want ONE
// penalty escalation per attempt, while GENUINELY separate attempts (esp-mqtt's
// reconnect cadence, or the pool's backoff-throttled retries -- seconds+ apart)
// each escalate.
//
// shouldEscalate() collapses events that fall within OFFBAND_FAIL_ESCALATE_WINDOW_MS
// of the last escalation into one. An explicit `active_` flag -- not
// last_ms_ == 0 -- gates the window, so a real escalation at millis()==0 is not
// mistaken for "no window" (which would let its paired event double-count).
// reset() is called on a clean connect so a failure AFTER a genuine reconnect
// escalates immediately instead of being masked by the pre-success window.
//
// A BEFORE_CONNECT-reset boolean was tried first and failed on hardware:
// connect-refused retries fire repeated ERRORs with no BEFORE_CONNECT between
// them, so the guard stayed set and the penalty stuck at 1 (a dead broker never
// reached Failed). The time-window does not depend on any such intervening event.

#ifndef OFFBAND_FAIL_ESCALATE_WINDOW_MS
  #define OFFBAND_FAIL_ESCALATE_WINDOW_MS 5000u
#endif

namespace offband {

class FailEscalateWindow {
public:
    // Returns true if this failure should count (escalate the penalty), false if
    // it collapses into the currently-open window. Slides the window forward to
    // now_ms each time it returns true.
    bool shouldEscalate(uint32_t now_ms) {
        if (active_ &&
            (uint32_t)(now_ms - last_ms_) < OFFBAND_FAIL_ESCALATE_WINDOW_MS) {
            return false;
        }
        active_ = true;
        last_ms_ = now_ms;
        return true;
    }

    // A clean connect ends the window: the next failure escalates immediately.
    void reset() { active_ = false; }

private:
    uint32_t last_ms_ = 0;
    bool     active_  = false;
};

}  // namespace offband
