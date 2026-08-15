#include <gtest/gtest.h>
#include <cstring>
#include "helpers/wifi_observer/MqttRingLog.h"

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

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
