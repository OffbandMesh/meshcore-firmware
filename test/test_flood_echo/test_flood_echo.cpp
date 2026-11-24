// #2249: FloodEchoTracker -- echo-confirmed retry decisions, positive and negative.
#include <gtest/gtest.h>
#include "helpers/FloodEchoTracker.h"

using offband::FloodEchoTracker;
using offband::floodEchoEligible;

namespace {

struct Hash { uint8_t b[MAX_HASH_SIZE]; };
Hash H(uint8_t seed) { Hash h; memset(h.b, seed, sizeof h.b); return h; }

mesh::Packet floodPkt(uint8_t payload_type, uint8_t hops, uint8_t tag = 0) {
    mesh::Packet p;
    p.header = ROUTE_TYPE_FLOOD | (payload_type << PH_TYPE_SHIFT);
    p.path_len = hops;            // 1-byte path hashes
    p.payload_len = 1;
    p.payload[0] = tag;
    return p;
}

const uint32_t kWin = 2600;

// A tracker that has already seen one echo, so retries are armed.
FloodEchoTracker armed(uint32_t now) {
    FloodEchoTracker t;
    t.onForwardSent(H(0xEE).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1), now, kWin);
    t.onHeard(H(0xEE).b, 2, now);
    return t;
}

}  // namespace

// --- echo before the deadline means no retry ---------------------------------
TEST(FloodEcho, EchoBeforeDeadline_NoRetry) {
    FloodEchoTracker t = armed(1000);
    t.onForwardSent(H(1).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1), 2000, kWin);
    t.onHeard(H(1).b, 2, 3000);
    EXPECT_EQ(t.takeDue(2000 + kWin + 1), nullptr);
    EXPECT_EQ(t.counters().echoed, 2u);
    EXPECT_EQ(t.pending(), 0);
}

// A sibling repeater at OUR depth carrying it also proves it is propagating.
TEST(FloodEcho, SameDepthCopy_CountsAsEcho) {
    FloodEchoTracker t = armed(1000);
    t.onForwardSent(H(1).b, 2, floodPkt(PAYLOAD_TYPE_GRP_TXT, 2), 2000, kWin);
    t.onHeard(H(1).b, 2, 2500);
    EXPECT_EQ(t.takeDue(2000 + kWin + 1), nullptr);
}

// --- no echo means exactly one retry, and it is the same packet --------------
TEST(FloodEcho, NoEcho_ExactlyOneRetry) {
    FloodEchoTracker t = armed(1000);
    mesh::Packet p = floodPkt(PAYLOAD_TYPE_GRP_TXT, 1, 0x5A);
    t.onForwardSent(H(1).b, 1, p, 2000, kWin);
    EXPECT_EQ(t.takeDue(2000 + kWin - 1), nullptr);          // not yet due
    const mesh::Packet* r = t.takeDue(2000 + kWin);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->payload[0], 0x5A);
    EXPECT_EQ(r->path_len, p.path_len);
    EXPECT_EQ(t.takeDue(2000 + kWin), nullptr);              // never handed out twice
    EXPECT_EQ(t.counters().retries, 1u);
}

// --- a retry that draws no echo never earns a second retry -------------------
TEST(FloodEcho, RetryWithoutEcho_NoSecondRetry) {
    FloodEchoTracker t = armed(1000);
    t.onForwardSent(H(1).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1), 2000, kWin);
    ASSERT_NE(t.takeDue(4600), nullptr);
    t.onForwardSent(H(1).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1), 5000, kWin);   // the retry is sent
    EXPECT_EQ(t.takeDue(5000 + kWin), nullptr);             // window closes: retired, not retried
    EXPECT_EQ(t.takeDue(60000), nullptr);
    EXPECT_EQ(t.counters().retries, 1u);
    EXPECT_EQ(t.pending(), 0);
}

TEST(FloodEcho, RetryThenEcho_CountsRetryEchoed) {
    FloodEchoTracker t = armed(1000);
    t.onForwardSent(H(1).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1), 2000, kWin);
    ASSERT_NE(t.takeDue(4600), nullptr);
    t.onForwardSent(H(1).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1), 5000, kWin);
    t.onHeard(H(1).b, 3, 6000);
    EXPECT_EQ(t.counters().retry_echoed, 1u);
    EXPECT_EQ(t.pending(), 0);
}

// A retry handed out but never sent is forgotten after the grace period.
TEST(FloodEcho, RetryNeverSent_SlotReleased) {
    FloodEchoTracker t = armed(1000);
    t.onForwardSent(H(1).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1), 2000, kWin);
    ASSERT_NE(t.takeDue(4600), nullptr);
    EXPECT_EQ(t.takeDue(4600 + FloodEchoTracker::kRetrySendGraceMs), nullptr);
    EXPECT_EQ(t.pending(), 0);
}

// --- a shorter-path copy is not an echo --------------------------------------
TEST(FloodEcho, ShorterPathCopy_IsNotEcho) {
    FloodEchoTracker t = armed(1000);
    t.onForwardSent(H(1).b, 3, floodPkt(PAYLOAD_TYPE_GRP_TXT, 3), 2000, kWin);
    t.onHeard(H(1).b, 2, 2500);                             // an upstream copy, late
    EXPECT_NE(t.takeDue(2000 + kWin), nullptr);
}

TEST(FloodEcho, OtherHash_IsNotEcho) {
    FloodEchoTracker t = armed(1000);
    t.onForwardSent(H(1).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1), 2000, kWin);
    t.onHeard(H(2).b, 4, 2500);
    EXPECT_NE(t.takeDue(2000 + kWin), nullptr);
}

// --- no downstream neighbour: retries are suppressed -------------------------
TEST(FloodEcho, NoEchoEverSinceBoot_Suppressed) {
    FloodEchoTracker t;
    t.onForwardSent(H(1).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1), 2000, kWin);
    EXPECT_EQ(t.takeDue(2000 + kWin), nullptr);
    EXPECT_EQ(t.counters().suppressed, 1u);
    EXPECT_EQ(t.counters().retries, 0u);
    EXPECT_EQ(t.pending(), 0);
}

TEST(FloodEcho, StaleNeighbour_Suppressed) {
    FloodEchoTracker t = armed(1000);
    uint32_t later = 1000 + FloodEchoTracker::kNeighbourStaleMs + 1;
    t.onForwardSent(H(1).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1), later, kWin);
    EXPECT_EQ(t.takeDue(later + kWin), nullptr);
    EXPECT_EQ(t.counters().suppressed, 1u);
}

TEST(FloodEcho, RecentNeighbour_NotSuppressed) {
    FloodEchoTracker t = armed(1000);
    uint32_t later = 1000 + FloodEchoTracker::kNeighbourStaleMs - kWin - 10;
    t.onForwardSent(H(1).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1), later, kWin);
    EXPECT_NE(t.takeDue(later + kWin), nullptr);
}

// --- a full table evicts the oldest entry ------------------------------------
TEST(FloodEcho, FullTable_EvictsOldest) {
    FloodEchoTracker t = armed(1000);
    for (uint8_t i = 1; i <= FloodEchoTracker::kSlots; ++i)
        t.onForwardSent(H(i).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1, i), 2000 + i * 10, kWin);
    t.onForwardSent(H(99).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1, 99), 2100, kWin);
    EXPECT_EQ(t.counters().evicted, 1u);
    EXPECT_EQ(t.pending(), FloodEchoTracker::kSlots);
    t.onHeard(H(1).b, 2, 2200);                             // evicted: no effect
    EXPECT_EQ(t.counters().echoed, 1u);                      // only armed()'s echo
}

// Gemini review of #2249: a retry whose slot is evicted before the retry is sent
// must not come back as a fresh first send and earn a SECOND retry.
TEST(FloodEcho, RetryEvictedBeforeSend_NoSecondRetry) {
    FloodEchoTracker t = armed(1000);
    t.onForwardSent(H(1).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1), 2000, kWin);
    ASSERT_NE(t.takeDue(4600), nullptr);                     // grace deadline 14600
    for (uint8_t i = 2; i <= FloodEchoTracker::kSlots + 1; ++i)    // longer deadlines
        t.onForwardSent(H(i).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1, i), 4700, 20000);
    EXPECT_GE(t.counters().evicted, 1u);                     // H(1) went
    t.onForwardSent(H(1).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1), 5000, kWin);   // retry sent
    EXPECT_EQ(t.takeDue(5000 + kWin), nullptr);
    EXPECT_EQ(t.counters().retries, 1u);
}

// Clock wrap: a deadline straddling 2^32 still fires on time, not 49 days late.
TEST(FloodEcho, DeadlineAcrossMillisWrap) {
    uint32_t near_wrap = 0xFFFFFF00u;
    FloodEchoTracker t = armed(near_wrap);
    t.onForwardSent(H(1).b, 1, floodPkt(PAYLOAD_TYPE_GRP_TXT, 1), near_wrap, kWin);
    EXPECT_EQ(t.takeDue(near_wrap + 100), nullptr);
    EXPECT_NE(t.takeDue(near_wrap + kWin), nullptr);
}

// --- eligibility ------------------------------------------------------------
TEST(FloodEcho, Eligibility) {
    EXPECT_TRUE(floodEchoEligible(floodPkt(PAYLOAD_TYPE_GRP_TXT, 1)));
    EXPECT_TRUE(floodEchoEligible(floodPkt(PAYLOAD_TYPE_TXT_MSG, 2)));
    EXPECT_FALSE(floodEchoEligible(floodPkt(PAYLOAD_TYPE_ADVERT, 1)));   // owner ruling
    EXPECT_FALSE(floodEchoEligible(floodPkt(PAYLOAD_TYPE_GRP_TXT, 0)));  // own origination
    mesh::Packet d = floodPkt(PAYLOAD_TYPE_GRP_TXT, 1);
    d.header = ROUTE_TYPE_DIRECT | (PAYLOAD_TYPE_GRP_TXT << PH_TYPE_SHIFT);
    EXPECT_FALSE(floodEchoEligible(d));
}

// --- echo window ------------------------------------------------------------
TEST(FloodEcho, EchoWindow) {
    // 582 ms airtime (a 165-byte channel message on the field preset), factor 0.5:
    // 3.5 airtimes + 500 ms.
    EXPECT_EQ(FloodEchoTracker::echoWindowMs(582, 0.5f), (uint32_t)(3.5f * 582) + 500);
    EXPECT_EQ(FloodEchoTracker::echoWindowMs(582, 0.0f), 582u + 500u);
    EXPECT_EQ(FloodEchoTracker::echoWindowMs(582, -1.0f), 582u + 500u);  // nonsense factor clamps
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
