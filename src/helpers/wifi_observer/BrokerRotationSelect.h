#pragma once

#include <cstdint>

// TLS broker promotion decision, extracted from MqttBrokerPool::workerLoop()
// so it can be unit-tested (#708).
//
// Deliberately dependency-free (no Arduino/ESP headers) so it builds in
// [env:native] -- same pattern as MqttRingLog.h. The pool owns the state; this
// header owns only the CHOICE of which queued TLS broker gets the next free
// budget slot. Non-TLS brokers never enter here: they hold no mbedTLS context,
// are exempt from the budget, and the pool promotes them unconditionally.
//
// WHY THIS EXISTS: the decision lived inline in a 40-line loop that could only
// run on hardware. The starvation defect it contained (#708) is invisible at two
// brokers and was validated at two, so nothing caught it. Anything that decides
// fairness belongs somewhere a test can reach.

namespace offband {

static constexpr uint8_t kNoSlot = 0xFF;

struct TlsCandidate {
    // Configured TLS/wss broker in a re-drivable state (Down/Backoff/Held*),
    // not mid-reconcile. Slots that are live, unconfigured, or non-TLS are not
    // candidates and should be passed with eligible=false.
    bool     eligible          = false;
    // Rotation cooldown still active -- the slot was just evicted and must not
    // immediately reclaim the budget it gave up (#175).
    bool     cooling           = false;
    // Service epoch when this broker was last promoted. 0 = never served.
    uint32_t last_served_epoch = 0;
};

// Choose which candidate gets the next TLS budget slot.
// Returns kNoSlot when the budget is full or nothing is promotable.
//
// CALLER CONTRACT -- READ THIS BEFORE WIRING IT UP.
// This function decides WHO WINS. It does not, and cannot, drive the losers.
// Every candidate that is not promoted must still be driven so it self-defers
// into HeldBudget, because the rotation scheduler only runs when at least one
// broker reads as "waiting". Skip that and the losers stay Down, nothing ever
// reads as waiting, rotation never fires, and the first broker to take the
// budget keeps it forever -- worse than the starvation this replaced.
//
// That exact mistake shipped once and was caught only on hardware (slot 3 held
// the budget 8.2 h while three enabled brokers sat Down). The unit tests take
// `eligible` as an INPUT, so they verify the choice and are structurally blind
// to the state transition that produces a candidate.
inline uint8_t selectTlsPromotion(const TlsCandidate* c, uint8_t n,
                                  uint8_t tls_live, uint8_t max_live) {
    if (c == nullptr || tls_live >= max_live) return kNoSlot;

    // ---------------------------------------------------------------------
    // OLDEST-SERVED-FIRST (#708).
    //
    // Replaces first-eligible-by-slot-index. Under index order the ONLY fairness
    // mechanism was the per-victim cooldown from #175, which uniquely determines
    // a winner at exactly TWO candidates and does nothing at three or more: only
    // one slot is ever cooling, so the lowest-indexed other candidate always won
    // and everything above it starved permanently. Measured on hv3-bench with
    // three enabled TLS brokers: 36 / 35 / 0 promotions over 481 samples.
    //
    // Ordering by service epoch fixes it structurally rather than by adding
    // another special case: a broker that has waited longest is promoted next,
    // regardless of index. Never-served slots carry epoch 0 and therefore win
    // immediately, which is precisely the starvation condition.
    //
    // Ties break to the lower index so the choice stays deterministic (matters
    // for the tests and for reproducible field behaviour).
    //
    // Epoch is a counter, not a timestamp: no millis() wraparound to reason
    // about. At one promotion per 60 s dwell it overflows uint32 in ~8000 years.
    // ---------------------------------------------------------------------
    uint8_t best = kNoSlot;
    for (uint8_t s = 0; s < n; ++s) {
        if (!c[s].eligible || c[s].cooling) continue;
        if (best == kNoSlot ||
            c[s].last_served_epoch < c[best].last_served_epoch) {
            best = s;
        }
    }
    return best;
}

}  // namespace offband
