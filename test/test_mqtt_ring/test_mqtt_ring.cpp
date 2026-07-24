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

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
