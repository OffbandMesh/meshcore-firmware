#include <gtest/gtest.h>
#include <vector>
#include "helpers/wifi_observer/BrokerRotationSelect.h"

using offband::TlsCandidate;
using offband::selectTlsPromotion;
using offband::kNoSlot;

// ---------------------------------------------------------------------------
// Rotation harness -- reproduces the pool's dwell cycle without hardware.
//
// Each cycle: the live broker is evicted and put on cooldown for one dwell, the
// previous victim's cooldown expires (cooldown == dwell in the firmware, so a
// victim becomes eligible exactly when the next eviction fires), then the
// scheduler picks who gets the freed budget.
// ---------------------------------------------------------------------------
struct Sim {
    std::vector<TlsCandidate> c;
    std::vector<int>          served;      // promotions per slot
    uint32_t epoch = 0;
    int      live  = -1;                   // slot currently holding the budget
    int      cooling_slot = -1;            // slot whose cooldown is active

    explicit Sim(uint8_t n) : c(n), served(n, 0) {
        for (auto& x : c) { x.eligible = true; x.cooling = false; x.last_served_epoch = 0; }
    }

    void cycle() {
        // Previous victim's cooldown expires as the next eviction lands.
        if (cooling_slot >= 0) c[cooling_slot].cooling = false;
        // Evict the live broker; it becomes a candidate again but is cooling.
        if (live >= 0) {
            c[live].eligible = true;
            c[live].cooling  = true;
            cooling_slot     = live;
            live             = -1;
        }
        uint8_t pick = selectTlsPromotion(c.data(), (uint8_t)c.size(),
                                          /*tls_live=*/0, /*max_live=*/1);
        if (pick != kNoSlot) {
            live = pick;
            c[pick].eligible = false;              // now live, not a candidate
            c[pick].last_served_epoch = ++epoch;   // stamp service
            served[pick]++;
        }
    }
    void run(int cycles) { for (int i = 0; i < cycles; ++i) cycle(); }
};

// ---------------------------------------------------------------------------
// N=2 -- the arity #175 was validated at. Passes with index-order selection,
// which is exactly why the defect shipped unnoticed.
// ---------------------------------------------------------------------------
TEST(BrokerRotation, TwoBrokersAlternateFairly) {
    Sim s(2);
    s.run(20);
    EXPECT_GT(s.served[0], 0);
    EXPECT_GT(s.served[1], 0);
    EXPECT_LE(std::abs(s.served[0] - s.served[1]), 1);
}

// ---------------------------------------------------------------------------
// N=3 -- the defect. Slot 2 must get turns; today it never does.
// Field-confirmed on hv3-bench: s1=36 s2=35 s3=0 over 481 samples.
// ---------------------------------------------------------------------------
TEST(BrokerRotation, ThreeBrokersAllGetServed) {
    Sim s(3);
    s.run(30);
    EXPECT_GT(s.served[0], 0);
    EXPECT_GT(s.served[1], 0);
    EXPECT_GT(s.served[2], 0) << "slot 2 starved -- index-ordered promotion";
}

// ---------------------------------------------------------------------------
// N=4 -- starvation widens: everything at index >= 2 is locked out.
// ---------------------------------------------------------------------------
TEST(BrokerRotation, FourBrokersAllGetServed) {
    Sim s(4);
    s.run(40);
    for (int i = 0; i < 4; ++i)
        EXPECT_GT(s.served[i], 0) << "slot " << i << " never promoted";
}

// ---------------------------------------------------------------------------
// Fairness, not just liveness: over many cycles no broker should get wildly
// more turns than another.
// ---------------------------------------------------------------------------
TEST(BrokerRotation, ThreeBrokersShareRoughlyEvenly) {
    Sim s(3);
    s.run(60);
    int lo = s.served[0], hi = s.served[0];
    for (int v : s.served) { lo = v < lo ? v : lo; hi = v > hi ? v : hi; }
    EXPECT_LE(hi - lo, 1) << "uneven: " << s.served[0] << "/" << s.served[1]
                          << "/" << s.served[2];
}

// ---------------------------------------------------------------------------
// A broker that has never been served must outrank one served recently, even
// when it sits at a higher slot index. This is the property index-order lacks.
// ---------------------------------------------------------------------------
TEST(BrokerRotation, NeverServedOutranksRecentlyServed) {
    TlsCandidate c[3];
    c[0].eligible = true; c[0].last_served_epoch = 9;   // served most recently
    c[1].eligible = true; c[1].last_served_epoch = 8;
    c[2].eligible = true; c[2].last_served_epoch = 0;   // never served
    EXPECT_EQ(selectTlsPromotion(c, 3, 0, 1), 2u);
}

// ---------------------------------------------------------------------------
// Oldest-served wins among slots that have all been served.
// ---------------------------------------------------------------------------
TEST(BrokerRotation, OldestServedWins) {
    TlsCandidate c[3];
    c[0].eligible = true; c[0].last_served_epoch = 7;
    c[1].eligible = true; c[1].last_served_epoch = 3;   // oldest
    c[2].eligible = true; c[2].last_served_epoch = 5;
    EXPECT_EQ(selectTlsPromotion(c, 3, 0, 1), 1u);
}

// ---------------------------------------------------------------------------
// Invariants that must survive the fix (these pass today -- regression guards).
// ---------------------------------------------------------------------------
TEST(BrokerRotation, CoolingSlotIsNeverSelected) {
    TlsCandidate c[2];
    c[0].eligible = true; c[0].cooling = true; c[0].last_served_epoch = 0;
    c[1].eligible = true; c[1].cooling = false; c[1].last_served_epoch = 5;
    EXPECT_EQ(selectTlsPromotion(c, 2, 0, 1), 1u);
}

TEST(BrokerRotation, FullBudgetPromotesNothing) {
    TlsCandidate c[2];
    c[0].eligible = true;
    c[1].eligible = true;
    EXPECT_EQ(selectTlsPromotion(c, 2, 1, 1), kNoSlot);
}

TEST(BrokerRotation, NoEligibleCandidatesReturnsNoSlot) {
    TlsCandidate c[3];   // all default: eligible = false
    EXPECT_EQ(selectTlsPromotion(c, 3, 0, 1), kNoSlot);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
