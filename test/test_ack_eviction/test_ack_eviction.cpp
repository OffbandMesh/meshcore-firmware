// Offband #999: the circular expected-ACK table silently destroys delivery confirmations.
//
// Send more than EXPECTED_ACK_TABLE_SIZE DMs before their ACKs return and the table wraps
// onto a slot that is still waiting. That message's confirmation is gone -- processAck()
// finds no match, no PUSH_CODE_SEND_CONFIRMED is sent, and the client shows a delivered
// message as unconfirmed with nothing logged anywhere.
//
// These tests pin the decision that makes it observable. The weight is on the ROLLOVER
// case and on the age of a slot that loses nothing, because those are the two ways a
// diagnostic can lie: an absurd age sends a reader hunting a clock bug, and an age printed
// for a free slot invents an eviction that did not happen.

#include <gtest/gtest.h>

#include <helpers/AckTable.h>

using namespace offband;

TEST(AckEviction, AFreeSlotLosesNothing) {
  const AckEviction e = ackEvictionOn(0, 1000, 5000);
  EXPECT_FALSE(e.loses_confirmation);
  EXPECT_EQ(0u, e.age_ms) << "a free slot's msg_sent is stale; its age must never be reported";
}

TEST(AckEviction, APendingSlotLosesItsConfirmation) {
  const AckEviction e = ackEvictionOn(0xDEADBEEF, 1000, 5000);
  EXPECT_TRUE(e.loses_confirmation);
  EXPECT_EQ(4000u, e.age_ms);
}

TEST(AckEviction, AgeIsCorrectAcrossTheMillisRollover) {
  // THE case unsigned arithmetic exists for. getMillis() is 32-bit and wraps about every
  // 49.7 days; a DM sent 512 ms before the wrap is evicted just after it. The true age is
  // 512 ms. The same subtraction in a signed type gives a large negative, which printed as
  // %lu becomes roughly 4.29e9 ms -- 49 days -- and reads as a broken clock.
  const uint32_t sent = 0xFFFFFF00u;
  const uint32_t now = 0x00000100u;
  const AckEviction e = ackEvictionOn(0x01020304, sent, now);
  EXPECT_TRUE(e.loses_confirmation);
  EXPECT_EQ(512u, e.age_ms);
  EXPECT_LT(e.age_ms, 1000u) << "a rollover must not read as a 49-day-old ACK";
}

TEST(AckEviction, AnAckEvictedInTheSameMillisecondIsAgeZeroAndStillALoss) {
  // Age zero is not "nothing was lost". The two are separate facts, and conflating them
  // would hide the worst case: a table so saturated that a send is evicted immediately.
  const AckEviction e = ackEvictionOn(0x11, 7777, 7777);
  EXPECT_TRUE(e.loses_confirmation);
  EXPECT_EQ(0u, e.age_ms);
}

TEST(AckEviction, EveryNonZeroHashCountsAsPending) {
  // The ack hash is opaque -- no value is reserved -- so only zero may mean "not waiting".
  EXPECT_FALSE(ackSlotPending(0));
  EXPECT_TRUE(ackSlotPending(1));
  EXPECT_TRUE(ackSlotPending(0x80000000u));
  EXPECT_TRUE(ackSlotPending(0xFFFFFFFFu))
      << "0xFFFFFFFF is a legitimate hash, not a sentinel";
}

TEST(AckEviction, AgeHelperIsPureUnsignedSubtraction) {
  EXPECT_EQ(0u, ackSlotAgeMs(0, 0));
  EXPECT_EQ(1u, ackSlotAgeMs(1, 0));
  EXPECT_EQ(1u, ackSlotAgeMs(0, 0xFFFFFFFFu)) << "wrap by one";
  EXPECT_EQ(0xFFFFFFFFu, ackSlotAgeMs(0xFFFFFFFEu, 0xFFFFFFFFu)) << "wrap the long way";
}

TEST(AckEviction, SlotsAdvanceCircularlyAndWrapAtTheEnd) {
  const int size = 8;   // EXPECTED_ACK_TABLE_SIZE today
  EXPECT_EQ(1, ackSlotAfter(0, size));
  EXPECT_EQ(7, ackSlotAfter(6, size));
  EXPECT_EQ(0, ackSlotAfter(7, size)) << "the wrap is where eviction happens";
  // One full lap returns to the start, which is what makes slot 0 the 9th send's victim.
  int idx = 0;
  for (int i = 0; i < size; i++) idx = ackSlotAfter(idx, size);
  EXPECT_EQ(0, idx);
}

TEST(AckEviction, ANonPositiveSizeCannotProduceANegativeIndex) {
  // Defensive: a misconfigured build must not index the array with -1.
  EXPECT_EQ(0, ackSlotAfter(5, 0));
  EXPECT_EQ(0, ackSlotAfter(5, -1));
  EXPECT_EQ(0, ackSlotAfter(0, 1)) << "a one-slot table evicts on every send";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
