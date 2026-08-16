#pragma once

#include <cstdint>

// Broker connection-health tracker (#739).
//
// Dependency-free (no Arduino/ESP headers) so it builds under [env:native] and
// is unit-testable in isolation -- same pattern as MqttRingLog.h and
// BrokerRotationSelect.h. The pool owns a BrokerHealth per broker and feeds it
// two events; this owns the POLICY of how a broker's reliability translates into
// backoff, rehabilitation, and a terminal give-up.
//
// WHY THIS EXISTS. Before #739 the only backoff state was retry_count, reset to 0
// on any success (MqttBroker::onConnected). That works for a DEAD broker but not
// a FLAKY one: an intermittent broker succeeds just often enough to zero its
// penalty, so it oscillates at the 5 s schedule floor forever and keeps claiming
// rotation turns equal to a reliable broker. Measured on hv3-bench: two remote
// JWT brokers at ~50% failure kept 46% of the soak with NO TLS broker up.
//
// The penalty is STICKY IN BOTH DIRECTIONS:
//   - a single success does NOT clear it (only a proven clean streak does), so a
//     flaky broker cannot cheaply reset;
//   - a broker must earn its way back with kSuccessBar CONSECUTIVE clean dwells,
//     at which point it is fully rehabilitated to a healthy running state.
// A failure escalates the penalty and zeroes any clean-streak progress. Past
// kTerminalPenalty the broker is declared Failed -- dropped from rotation and
// surfaced loudly -- instead of churning a rotation turn forever.

namespace offband {

// Consecutive clean full-dwells required to fully rehabilitate a broker whose
// penalty is non-zero. One dwell is ~60 s, so 3 = ~3 min of unbroken service.
#ifndef OFFBAND_BROKER_SUCCESS_BAR
  #define OFFBAND_BROKER_SUCCESS_BAR 3u
#endif

// Penalty level at which a broker is declared terminally Failed and removed from
// the rotation candidate set. The backoff schedule has 5 steps (5..120 s); this
// gives several full escalations before giving up.
#ifndef OFFBAND_BROKER_TERMINAL_PENALTY
  #define OFFBAND_BROKER_TERMINAL_PENALTY 8u
#endif

struct BrokerHealth {
    uint16_t fail_penalty = 0;   // escalation level; indexes the backoff schedule
    uint16_t clean_streak = 0;   // consecutive clean full-dwells since last failure

    // A connection failed (disconnect-with-error, handshake failure, refused).
    // Escalates the penalty and discards any clean-streak progress -- a single
    // failure means the broker has NOT proven stable.
    void onFailure() {
        if (fail_penalty < 0xFFFF) fail_penalty++;
        clean_streak = 0;
    }

    // The broker held the budget for a FULL dwell and was rotated out cleanly
    // (no error). This is the only event that earns rehabilitation -- NOT a bare
    // connect, which a flaky broker does constantly. Only kSuccessBar of these in
    // a row clears the penalty.
    void onCleanDwell() {
        if (fail_penalty == 0) return;        // already healthy, nothing to earn
        if (++clean_streak >= OFFBAND_BROKER_SUCCESS_BAR) {
            fail_penalty = 0;                 // rehabilitated
            clean_streak = 0;
        }
    }

    // True once the broker has failed enough to be given up on. The pool moves it
    // to BrokerState::Failed, drops it from rotation, and logs loudly.
    bool isTerminal() const { return fail_penalty >= OFFBAND_BROKER_TERMINAL_PENALTY; }

    // Healthy = never penalised (or fully rehabilitated). A broker with any
    // pending penalty is "recovering" and rotation should prefer healthy peers.
    bool isHealthy() const { return fail_penalty == 0; }

    void reset() { fail_penalty = 0; clean_streak = 0; }
};

}  // namespace offband
