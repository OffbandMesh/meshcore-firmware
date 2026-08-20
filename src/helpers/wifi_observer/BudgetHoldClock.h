#pragma once
#include <cstdint>

// Reconnect-proof clock for how long a broker has held the single TLS budget
// slot, used to decide rotation eviction (#720). Dependency-free (no
// Arduino/ESP headers) so it builds in [env:native] -- same pattern as
// BrokerHealth / FailEscalateWindow / BrokerRotationSelect.
//
// WHY THIS EXISTS: rotation eviction used `went_up_ms` ("connected since") for
// the victim's age. `went_up_ms` is re-stamped on EVERY transition to Up,
// including esp-mqtt's silent auto-reconnect. With OFFBAND_MAX_LIVE_TLS = 1
// there is one Up broker at a time; if it flaps faster than the dwell, its age
// resets below the eviction threshold every pass, rotation never fires, and
// every other enabled TLS broker is starved forever (the observer looks healthy:
// one broker Up, no errors). Eviction needs a clock a reconnect does NOT reset.
//
// A `held_` flag -- not `since_ms_ == 0` -- marks occupancy, so a real
// acquisition at millis()==0 is unambiguous (same sentinel discipline as
// FailEscalateWindow). onConnected() stamps the start ONCE per occupancy;
// reconnects (auto or pool-driven Backoff->retry, which keep the client and do
// not release the budget) leave it. onReleased() clears it when the client is
// destroyed (rotation eviction or terminal reap), so the next acquisition
// stamps fresh.

namespace offband {

class BudgetHoldClock {
public:
    // Call on each transition to Up. Records the hold start once per occupancy;
    // a reconnect (held_ already true) does not move it.
    void onConnected(uint32_t now_ms) {
        if (!held_) { held_ = true; since_ms_ = now_ms; }
    }

    // Call when the budget is released -- the client is destroyed (rotation
    // eviction or terminal-Failed reap). The next onConnected() starts fresh.
    void onReleased() { held_ = false; since_ms_ = 0; }

    bool held() const { return held_; }

    // How long the budget has been continuously held, wrap-safe. 0 if not held.
    uint32_t heldMs(uint32_t now_ms) const {
        return held_ ? (uint32_t)(now_ms - since_ms_) : 0;
    }

private:
    bool     held_     = false;
    uint32_t since_ms_ = 0;
};

}  // namespace offband
