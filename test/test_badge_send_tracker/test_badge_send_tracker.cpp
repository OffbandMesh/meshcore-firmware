// Native unit tests for the badge's DM tracker (#1227): a DM typed on the badge goes
// from its first attempt to delivered or failed. Every attempt has its own expected
// ACK, retries wait for the previous attempt's timeout, and a late ACK still counts.

#include <gtest/gtest.h>
#include "helpers/BadgeSendTracker.h"

using offband::BadgeSend;
// 2 slots, 3 attempts, 16-byte text, 32-byte keys (PUB_KEY_SIZE, as on the badge)
using Tracker = offband::BadgeSendTracker<2, 3, 16, 32>;

namespace {
// Two keys that share their first six bytes: only the whole key tells them apart.
const uint8_t kKey[32] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                          17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
const uint8_t kOtherKey[32] = {1, 2, 3, 4, 5, 6, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
                               9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
}  // namespace

TEST(BadgeSendTracker, AckForTheFirstAttemptDelivers) {
  Tracker t;
  const uint16_t h = t.begin(kKey, 1000, "hi");
  ASSERT_NE(0, h);
  t.sent(h, 0xAAAA0001, 0, 5000);
  EXPECT_EQ(BadgeSend::Sending, t.status(h));
  EXPECT_EQ(h, t.ack(0xAAAA0001));
  EXPECT_EQ(BadgeSend::Delivered, t.status(h));
  EXPECT_EQ(0, t.due(999999));   // a delivered DM is never retried
}

TEST(BadgeSendTracker, TimeoutAsksForTheNextAttempt) {
  Tracker t;
  const uint16_t h = t.begin(kKey, 1000, "hi");
  t.sent(h, 0xAAAA0001, 0, 5000);
  EXPECT_EQ(0, t.due(4999));
  EXPECT_EQ(h, t.due(5000));
  const Tracker::Slot* s = t.find(h);
  ASSERT_NE(nullptr, s);
  EXPECT_EQ(1, s->attempts);           // the next attempt is number 1
  EXPECT_EQ(1000u, s->timestamp);      // retries reuse the first timestamp...
  EXPECT_STREQ("hi", s->text);         // ...and the text
  EXPECT_EQ(0, memcmp(kKey, s->key, sizeof(kKey)));   // the whole key, not a prefix
  t.sent(h, 0xAAAA0002, 5000, 5000);
  EXPECT_EQ(0, t.due(9999));
  EXPECT_EQ(BadgeSend::Sending, t.status(h));
}

TEST(BadgeSendTracker, LateAckForAnEarlierAttemptStillDelivers) {
  Tracker t;
  const uint16_t h = t.begin(kKey, 1000, "hi");
  t.sent(h, 0xAAAA0001, 0, 5000);
  ASSERT_EQ(h, t.due(5000));
  t.sent(h, 0xAAAA0002, 5000, 5000);
  EXPECT_EQ(h, t.ack(0xAAAA0001));     // the first attempt's ACK, arriving late
  EXPECT_EQ(BadgeSend::Delivered, t.status(h));
}

TEST(BadgeSendTracker, FailsAfterTheLastAttemptTimesOut) {
  Tracker t;
  const uint16_t h = t.begin(kKey, 1000, "hi");
  uint32_t now = 0;
  for (uint32_t ack = 1; ack <= 3; ack++) {
    t.sent(h, ack, now, 5000);
    now += 5000;
    if (ack < 3) {
      ASSERT_EQ(h, t.due(now));
    }
  }
  EXPECT_EQ(0, t.due(now));            // no fourth attempt
  EXPECT_EQ(BadgeSend::Failed, t.status(h));
}

TEST(BadgeSendTracker, LateAckAfterFailureMarksDelivered) {
  Tracker t;
  const uint16_t h = t.begin(kKey, 1000, "hi");
  t.sent(h, 0xAAAA0001, 0, 5000);
  t.due(5000);
  t.sent(h, 0xAAAA0002, 5000, 5000);
  t.due(10000);
  t.sent(h, 0xAAAA0003, 10000, 5000);
  t.due(15000);
  ASSERT_EQ(BadgeSend::Failed, t.status(h));
  EXPECT_EQ(h, t.ack(0xAAAA0002));     // the recipient got it after all
  EXPECT_EQ(BadgeSend::Delivered, t.status(h));
}

TEST(BadgeSendTracker, RepeatedAckReturnsTheSameHandle) {
  Tracker t;
  const uint16_t h = t.begin(kKey, 1000, "hi");
  t.sent(h, 0xAAAA0001, 0, 5000);
  EXPECT_EQ(h, t.ack(0xAAAA0001));
  EXPECT_EQ(h, t.ack(0xAAAA0001));     // MeshCore can deliver the same ACK twice
  EXPECT_EQ(BadgeSend::Delivered, t.status(h));
}

TEST(BadgeSendTracker, UnknownOrZeroAckIsNotOurs) {
  Tracker t;
  const uint16_t h = t.begin(kKey, 1000, "hi");
  t.sent(h, 0xAAAA0001, 0, 5000);
  EXPECT_EQ(0, t.ack(0xBBBB0001));
  EXPECT_EQ(0, t.ack(0));              // 0 means "no ACK expected" in MeshCore
  EXPECT_EQ(BadgeSend::Sending, t.status(h));
}

// Retries find the recipient by the key a DM keeps, so two recipients whose keys start
// alike must still each keep their own.
TEST(BadgeSendTracker, KeepsEachRecipientsWholeKey) {
  Tracker t;
  const uint16_t a = t.begin(kKey, 1, "a");
  const uint16_t b = t.begin(kOtherKey, 2, "b");
  ASSERT_NE(nullptr, t.find(a));
  ASSERT_NE(nullptr, t.find(b));
  EXPECT_EQ(0, memcmp(kKey, t.find(a)->key, sizeof(kKey)));
  EXPECT_EQ(0, memcmp(kOtherKey, t.find(b)->key, sizeof(kOtherKey)));
}

TEST(BadgeSendTracker, FullTableRefusesThenReusesAFinishedSlot) {
  Tracker t;
  const uint16_t a = t.begin(kKey, 1, "a");
  const uint16_t b = t.begin(kOtherKey, 2, "b");
  ASSERT_NE(0, a);
  ASSERT_NE(0, b);
  EXPECT_NE(a, b);
  t.sent(a, 11, 0, 5000);
  t.sent(b, 22, 0, 5000);
  EXPECT_EQ(0, t.begin(kKey, 3, "c"));   // both still sending
  ASSERT_EQ(a, t.ack(11));
  const uint16_t c = t.begin(kKey, 3, "c");
  ASSERT_NE(0, c);
  EXPECT_NE(a, c);                       // a fresh handle, never the old one
  EXPECT_EQ(BadgeSend::None, t.status(a));
  EXPECT_EQ(0, t.ack(11));               // the reused slot forgot the old ACK
}

TEST(BadgeSendTracker, TextLongerThanTheCapIsRefused) {
  Tracker t;
  EXPECT_EQ(0, t.begin(kKey, 1, "0123456789abcdefX"));   // 17 bytes, cap 16
  EXPECT_NE(0, t.begin(kKey, 1, "0123456789abcdef"));    // exactly 16
  EXPECT_EQ(0, t.begin(nullptr, 1, "x"));
  EXPECT_EQ(0, t.begin(kKey, 1, nullptr));
}

TEST(BadgeSendTracker, DeadlineSurvivesMillisWrap) {
  Tracker t;
  const uint16_t h = t.begin(kKey, 1000, "hi");
  t.sent(h, 0xAAAA0001, 0xFFFFF000u, 0x2000);   // the deadline wraps past zero
  EXPECT_EQ(0, t.due(0xFFFFFFFFu));
  EXPECT_EQ(0, t.due(0x00000FFFu));
  EXPECT_EQ(h, t.due(0x00001000u));
}

// A zero timeout from the mesh can't make the caller's loop send the same DM twice in
// one pass: every attempt waits at least kMinTimeoutMs.
TEST(BadgeSendTracker, ZeroTimeoutStillWaitsTheMinimum) {
  Tracker t;
  const uint16_t h = t.begin(kKey, 1000, "hi");
  t.sent(h, 0xAAAA0001, 5000, 0);
  EXPECT_EQ(0, t.due(5000));
  EXPECT_EQ(0, t.due(5000 + Tracker::kMinTimeoutMs - 1));
  EXPECT_EQ(h, t.due(5000 + Tracker::kMinTimeoutMs));
}

TEST(BadgeSendTracker, SendFailedMarksFailed) {
  Tracker t;
  const uint16_t h = t.begin(kKey, 1000, "hi");
  t.sendFailed(h);
  EXPECT_EQ(BadgeSend::Failed, t.status(h));
  EXPECT_EQ(0, t.due(999999));
}

TEST(BadgeSendTracker, StatusOfAnUnknownHandleIsNone) {
  Tracker t;
  EXPECT_EQ(BadgeSend::None, t.status(0));
  EXPECT_EQ(BadgeSend::None, t.status(1234));
  EXPECT_EQ(nullptr, t.find(0));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
