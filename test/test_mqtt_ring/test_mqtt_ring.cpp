#include <gtest/gtest.h>
#include <cstring>
#include <vector>
// MqttPayload.h pulls WifiObserverConfig.h, which #errors unless a role is
// declared. The record layout is role-independent, so declare the pool role
// for this translation unit only -- keeps MqttRingLog.h itself dependency-free.
#ifndef OFFBAND_MQTT_POOL
#define OFFBAND_MQTT_POOL 1
#endif
#include "helpers/wifi_observer/MqttRingLog.h"
#include "helpers/wifi_observer/MqttPayload.h"

static void fill(uint8_t* b, size_t n, uint8_t v) { memset(b, v, n); }

TEST(MqttRingLog, AppendAndReadInOrder) {
    MqttRingLog ring;
    uint8_t a[8]; fill(a, sizeof(a), 0xA1);
    uint8_t b[8]; fill(b, sizeof(b), 0xB2);
    EXPECT_EQ(ring.append(a, sizeof(a)), 1u);   // seq starts at 1
    EXPECT_EQ(ring.append(b, sizeof(b)), 2u);
    EXPECT_EQ(ring.head(), 2u);

    uint8_t out[MQTT_RING_MSG_MAX]; size_t out_len = 0; uint32_t seq = 0;
    ASSERT_TRUE(ring.peek(0, out, sizeof(out), out_len, seq));
    EXPECT_EQ(seq, 1u);
    EXPECT_EQ(out_len, sizeof(a));
    EXPECT_EQ(memcmp(out, a, sizeof(a)), 0);
    ring.commit(0);
    ASSERT_TRUE(ring.peek(0, out, sizeof(out), out_len, seq));
    EXPECT_EQ(seq, 2u);
    ring.commit(0);
    EXPECT_FALSE(ring.peek(0, out, sizeof(out), out_len, seq));  // drained
}

TEST(MqttRingLog, ReadersAreIndependent) {
    MqttRingLog ring;
    uint8_t m[4]; fill(m, sizeof(m), 0x5A);
    ring.append(m, sizeof(m));
    ring.append(m, sizeof(m));
    uint8_t out[MQTT_RING_MSG_MAX]; size_t n = 0; uint32_t seq = 0;

    ASSERT_TRUE(ring.peek(0, out, sizeof(out), n, seq));
    ring.commit(0);                       // reader 0 consumed one
    EXPECT_EQ(ring.lag(0), 1u);
    EXPECT_EQ(ring.lag(1), 2u);           // reader 1 untouched
}

TEST(MqttRingLog, LapDetectedAndReaderResyncs) {
    MqttRingLog ring;
    uint8_t m[4]; fill(m, sizeof(m), 0x77);
    for (int i = 0; i < MQTT_RING_SLOTS + 3; i++) ring.append(m, sizeof(m));
    EXPECT_TRUE(ring.lapped(0));                       // reader 0 never read
    EXPECT_EQ(ring.lag(0), (uint32_t)MQTT_RING_SLOTS); // clamped to retained
    ring.resync(0);
    EXPECT_FALSE(ring.lapped(0));
    EXPECT_EQ(ring.lag(0), 0u);
}

TEST(MqttRingLog, OversizeRejected) {
    MqttRingLog ring;
    uint8_t big[MQTT_RING_MSG_MAX + 1] = {0};
    EXPECT_EQ(ring.append(big, sizeof(big)), 0u);  // 0 = rejected
    EXPECT_EQ(ring.head(), 0u);
}

// ---------------------------------------------------------------------------
// #710: publish-ring overrun must be COUNTED, not silently clamped.
//
// The ring drops messages when the writer overruns a stalled reader (a broker
// rotated out for a TLS dwell). Today that loss is invisible: the cursor is
// silently clamped to the oldest retained message, nothing is counted, nothing
// is logged, and lapped() -- the only indicator -- is derived from the current
// cursor, so it reads false again the moment the reader catches up.
// ---------------------------------------------------------------------------

TEST(MqttRingLogDrops, CountIsExactAfterOverrun) {
    MqttRingLog ring;
    uint8_t m[4]; fill(m, sizeof(m), 0x11);

    // Overrun reader 0 by exactly 4 messages.
    const int N = MQTT_RING_SLOTS + 4;
    for (int i = 0; i < N; i++) ring.append(m, sizeof(m));
    // head=N, tail=N-SLOTS+1, so seqs 1..4 are destroyed before reader 0 read them.
    ASSERT_TRUE(ring.lapped(0));

    uint8_t out[MQTT_RING_MSG_MAX]; size_t out_len = 0; uint32_t seq = 0;
    ASSERT_TRUE(ring.peek(0, out, sizeof(out), out_len, seq));
    EXPECT_EQ(seq, (uint32_t)(N - MQTT_RING_SLOTS + 1));  // jumped to oldest kept
    ring.commit(0);                                       // the clamp that loses 4

    EXPECT_EQ(ring.droppedCount(0), 4u);
}

TEST(MqttRingLogDrops, CountSurvivesReaderCatchingUp) {
    MqttRingLog ring;
    uint8_t m[4]; fill(m, sizeof(m), 0x22);

    const int N = MQTT_RING_SLOTS + 4;
    for (int i = 0; i < N; i++) ring.append(m, sizeof(m));

    uint8_t out[MQTT_RING_MSG_MAX]; size_t out_len = 0; uint32_t seq = 0;
    while (ring.peek(0, out, sizeof(out), out_len, seq)) ring.commit(0);

    // The transient flag has erased its own evidence -- this is the defect.
    EXPECT_FALSE(ring.lapped(0));
    EXPECT_EQ(ring.lag(0), 0u);
    // The durable counter must NOT erase.
    EXPECT_EQ(ring.droppedCount(0), 4u);
}

TEST(MqttRingLogDrops, NoDropsWhenReaderKeepsUp) {
    MqttRingLog ring;
    uint8_t m[4]; fill(m, sizeof(m), 0x33);
    uint8_t out[MQTT_RING_MSG_MAX]; size_t out_len = 0; uint32_t seq = 0;

    for (int i = 0; i < MQTT_RING_SLOTS * 3; i++) {
        ring.append(m, sizeof(m));
        ASSERT_TRUE(ring.peek(0, out, sizeof(out), out_len, seq));
        ring.commit(0);
    }
    EXPECT_FALSE(ring.lapped(0));
    EXPECT_EQ(ring.droppedCount(0), 0u);
}

TEST(MqttRingLogDrops, CountIsPerReaderNotGlobal) {
    MqttRingLog ring;
    uint8_t m[4]; fill(m, sizeof(m), 0x44);
    uint8_t out[MQTT_RING_MSG_MAX]; size_t out_len = 0; uint32_t seq = 0;

    // Reader 0 keeps up; reader 1 never reads.
    for (int i = 0; i < MQTT_RING_SLOTS + 5; i++) {
        ring.append(m, sizeof(m));
        if (ring.peek(0, out, sizeof(out), out_len, seq)) ring.commit(0);
    }
    ring.commit(1);   // reader 1 finally advances, discovering the loss

    EXPECT_EQ(ring.droppedCount(0), 0u);
    EXPECT_EQ(ring.droppedCount(1), 5u);
}

TEST(MqttRingLogDrops, RepeatedOverrunsAccumulate) {
    MqttRingLog ring;
    uint8_t m[4]; fill(m, sizeof(m), 0x55);
    uint8_t out[MQTT_RING_MSG_MAX]; size_t out_len = 0; uint32_t seq = 0;

    // First overrun: lose 2, then resume.
    for (int i = 0; i < MQTT_RING_SLOTS + 2; i++) ring.append(m, sizeof(m));
    while (ring.peek(0, out, sizeof(out), out_len, seq)) ring.commit(0);
    ASSERT_EQ(ring.droppedCount(0), 2u);

    // Second overrun: lose 3 more. Counter accumulates, never resets.
    for (int i = 0; i < MQTT_RING_SLOTS + 3; i++) ring.append(m, sizeof(m));
    while (ring.peek(0, out, sizeof(out), out_len, seq)) ring.commit(0);
    EXPECT_EQ(ring.droppedCount(0), 5u);
}

// ---------------------------------------------------------------------------
// #723: a rotated-out reader must KEEP its cursor across re-attach.
//
// v1.5.0-beta1 resynced on every rotation re-entry, discarding exactly the
// backlog the ring exists to hold. Field symptom: an observer published the
// first message and silently dropped the next two, which had arrived while its
// broker was rotated out. resync() is for FIRST attach only.
// ---------------------------------------------------------------------------

TEST(MqttRingLogDrops, BacklogSurvivesWhenReaderIsAwayAndReturns) {
    MqttRingLog ring;
    uint8_t m[4]; fill(m, sizeof(m), 0x66);
    uint8_t out[MQTT_RING_MSG_MAX]; size_t out_len = 0; uint32_t seq = 0;

    // Reader 0 is up and consumes message 1.
    ring.append(m, sizeof(m));
    ASSERT_TRUE(ring.peek(0, out, sizeof(out), out_len, seq));
    EXPECT_EQ(seq, 1u);
    ring.commit(0);

    // Reader 0 rotates OUT. Two more messages arrive while it is away.
    ring.append(m, sizeof(m));   // seq 2
    ring.append(m, sizeof(m));   // seq 3

    // Reader 0 rotates back IN. Without a resync it must resume at seq 2 --
    // the backlog is the whole point of the ring.
    ASSERT_TRUE(ring.peek(0, out, sizeof(out), out_len, seq))
        << "backlog discarded on re-entry -- the beta1 regression";
    EXPECT_EQ(seq, 2u);
    ring.commit(0);
    ASSERT_TRUE(ring.peek(0, out, sizeof(out), out_len, seq));
    EXPECT_EQ(seq, 3u);
    ring.commit(0);

    EXPECT_FALSE(ring.peek(0, out, sizeof(out), out_len, seq));  // fully caught up
    EXPECT_EQ(ring.droppedCount(0), 0u);                         // nothing lost
}

TEST(MqttRingLogDrops, ResyncStillSkipsBacklogWhenExplicitlyCalled) {
    MqttRingLog ring;
    uint8_t m[4]; fill(m, sizeof(m), 0x77);
    uint8_t out[MQTT_RING_MSG_MAX]; size_t out_len = 0; uint32_t seq = 0;

    for (int i = 0; i < 5; i++) ring.append(m, sizeof(m));
    ring.resync(0);                       // first-attach behaviour (#710 intent)
    EXPECT_FALSE(ring.peek(0, out, sizeof(out), out_len, seq));
    EXPECT_EQ(ring.lag(0), 0u);
    EXPECT_EQ(ring.droppedCount(0), 0u);  // deliberate skip is not a drop

    ring.append(m, sizeof(m));            // and it tracks new traffic from there
    ASSERT_TRUE(ring.peek(0, out, sizeof(out), out_len, seq));
    EXPECT_EQ(seq, 6u);
}

// ---------------------------------------------------------------------------
// #726: the ring must accept anything publishParsedPacket() can build.
//
// That function writes the /packets JSON into char json[1024]. The ring capped
// at 512, so every payload from 513..1023 bytes was refused and discarded with
// no log and no counter -- a ~98-byte packet ceiling, invisible. These pin the
// contract: MQTT_RING_MSG_MAX >= the builder's buffer, and a refusal is COUNTED.
// ---------------------------------------------------------------------------

TEST(MqttRingLogDrops, AcceptsAnythingTheWriterCanProduce) {
    // This test began life under #726, asserting the ring could hold a 1023-byte
    // RENDERED JSON body -- because back then publishParsedPacket() wrote the
    // finished body into the ring and the ring capped at 512, silently refusing
    // anything larger (a ~98-byte packet ceiling, invisible in the field).
    //
    // #727 changed what the writer produces: the ring now stores a PacketRecord
    // and drainBroker() renders per broker. So the invariant is unchanged in
    // spirit -- the ring must accept whatever the writer hands it -- but the
    // thing being handed over is now the record, not the body.
    MqttRingLog ring;
    std::vector<uint8_t> rec(sizeof(offband::PacketRecord), 0x5A);
    EXPECT_EQ(ring.append(rec.data(), rec.size()), 1u)
        << "ring refuses a record the writer can legally produce";
    EXPECT_EQ(ring.rejectedCount(), 0u);

    // And a full rendered body is now correctly refused -- nothing writes one.
    std::vector<uint8_t> old_body(1023, 0x5B);
    EXPECT_EQ(ring.append(old_body.data(), old_body.size()), 0u);
    EXPECT_EQ(ring.rejectedCount(), 1u);   // refused, and COUNTED
}

TEST(MqttRingLogDrops, OversizeIsCountedNotSilent) {
    MqttRingLog ring;
    std::vector<uint8_t> toobig(MQTT_RING_MSG_MAX + 1, 0x5C);
    EXPECT_EQ(ring.append(toobig.data(), toobig.size()), 0u);
    EXPECT_EQ(ring.rejectedCount(), 1u);          // counted, not discarded quietly
    EXPECT_EQ(ring.droppedCount(0), 0u);          // and NOT confused with overrun
    EXPECT_EQ(ring.head(), 0u);                   // nothing entered the ring
}

// ---------------------------------------------------------------------------
// #727: the ring now stores a PacketRecord, not a rendered JSON body.
//
// These pin the invariants that made #726 possible, and the one this redesign
// would otherwise have introduced (drain-time timestamps).
// ---------------------------------------------------------------------------

TEST(MqttRingRecord, RecordFitsTheRing) {
    // The whole point of #727: the writer's record must fit what the ring takes.
    // #726 was this exact invariant broken silently, at a different size.
    EXPECT_LE(sizeof(offband::PacketRecord), (size_t)MQTT_RING_MSG_MAX);
}

TEST(MqttRingRecord, MaxSizePacketRoundTrips) {
    MqttRingLog ring;
    offband::PacketRecord in{};
    in.rx_time      = 1755300000u;
    in.rssi         = -97;
    in.snr          = -7.5f;
    in.score        = 123;
    in.raw_len      = 255;                       // worst-case MeshCore packet
    for (int i = 0; i < 255; i++) in.raw[i] = (uint8_t)i;

    ASSERT_EQ(ring.append(reinterpret_cast<const uint8_t*>(&in), sizeof(in)), 1u)
        << "a max-size packet record must be accepted -- this is #726's failure mode";
    EXPECT_EQ(ring.rejectedCount(), 0u);

    uint8_t out[MQTT_RING_MSG_MAX]; size_t len = 0; uint32_t seq = 0;
    ASSERT_TRUE(ring.peek(0, out, sizeof(out), len, seq));
    ASSERT_EQ(len, sizeof(offband::PacketRecord));

    offband::PacketRecord got{};
    memcpy(&got, out, sizeof(got));
    EXPECT_EQ(got.rx_time, in.rx_time);
    EXPECT_EQ(got.rssi, in.rssi);
    EXPECT_FLOAT_EQ(got.snr, in.snr);
    EXPECT_EQ(got.score, in.score);
    EXPECT_EQ(got.raw_len, 255);
    EXPECT_EQ(memcmp(got.raw, in.raw, 255), 0);   // packet bytes survive intact
}

TEST(MqttRingRecord, ReceiveTimeSurvivesADelayedDrain) {
    // The bug this redesign would have introduced. Rendering moved to drain, so
    // a record sitting through a rotation dwell must still carry the RECEIVE
    // time -- calling time(nullptr) at render would stamp the drain time and
    // silently mis-timestamp exactly the rotated-out case the ring exists for.
    MqttRingLog ring;
    const uint32_t rx = 1755300000u;


    offband::PacketRecord in{};
    in.rx_time = rx;
    in.raw_len = 32;
    ASSERT_EQ(ring.append(reinterpret_cast<const uint8_t*>(&in), sizeof(in)), 1u);

    // Broker is away; unrelated traffic arrives meanwhile.
    for (int i = 0; i < 5; i++) {
        offband::PacketRecord other{};
        other.rx_time = rx + 60u * (uint32_t)(i + 1);
        other.raw_len = 16;
        ring.append(reinterpret_cast<const uint8_t*>(&other), sizeof(other));
    }

    // Broker returns and drains: the FIRST record must still carry its own rx.
    uint8_t out[MQTT_RING_MSG_MAX]; size_t len = 0; uint32_t seq = 0;
    ASSERT_TRUE(ring.peek(0, out, sizeof(out), len, seq));
    offband::PacketRecord got{};
    memcpy(&got, out, sizeof(got));
    EXPECT_EQ(got.rx_time, rx) << "record carried the wrong time across a delayed drain";
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
