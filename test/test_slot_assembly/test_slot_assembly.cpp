// Native unit tests for assembling a pushed tune (#1478).
//
// A tune is up to 240 characters and a frame carries at most 176 -- 20 over BLE before
// MTU negotiation -- so a pushed tune always arrives in pieces. These tests cover what
// happens when those pieces arrive wrong, which on a radio link is not hypothetical.

#include <gtest/gtest.h>
#include <cstring>
#include <string>

#include <helpers/ui/SlotAssembly.h>

using offband::SlotAssembly;
using R = SlotAssembly<240>::Result;

namespace {
// Feed a string as `chunk`-sized pieces, returning the last result.
R feedAll(SlotAssembly<240>& a, uint8_t slot, const std::string& s, size_t chunk) {
  R last = R::Accepted;
  size_t sent = 0;
  while (sent < s.size()) {
    const size_t n = (s.size() - sent < chunk) ? s.size() - sent : chunk;
    const bool more = (sent + n) < s.size();
    last = a.feed(slot, (uint8_t)sent, more, (const uint8_t*)s.data() + sent, n);
    if (last != R::Accepted && last != R::Complete) return last;
    sent += n;
  }
  return last;
}
const std::string kTune = "Nokia:d=4,o=5,b=63:8e6,8d6,4f#,4g#,8c#6,8b,4d,4e,8b,8a,4c#,4e,2a";
}  // namespace

TEST(SlotAssembly, AssemblesAcrossChunksOfAnySize) {
  // 20 is the BLE floor before MTU negotiation, which is the worst case this must take.
  for (size_t chunk : {1u, 7u, 20u, 64u, 173u}) {
    SlotAssembly<240> a;
    ASSERT_EQ(R::Complete, feedAll(a, 3, kTune, chunk)) << "chunk size " << chunk;
    EXPECT_EQ(kTune, std::string(a.text())) << "chunk size " << chunk;
    EXPECT_EQ(kTune.size(), a.length());
  }
}

TEST(SlotAssembly, OneChunkThatIsAlsoTheLastCompletesImmediately) {
  SlotAssembly<240> a;
  const char* s = "Tick:d=32,o=8,b=120:c";
  EXPECT_EQ(R::Complete, a.feed(0, 0, false, (const uint8_t*)s, strlen(s)));
  EXPECT_STREQ(s, a.text());
}

// A dropped frame is the realistic radio failure, and it must be REPORTED rather than
// silently producing a tune with a hole in it.
TEST(SlotAssembly, ADroppedChunkIsRefusedNotStitched) {
  SlotAssembly<240> a;
  const char* p1 = "Nokia:d=4,";
  const char* p3 = "4f#,4g#";
  ASSERT_EQ(R::Accepted, a.feed(1, 0, true, (const uint8_t*)p1, strlen(p1)));
  // Offset jumps past the missing middle chunk.
  EXPECT_EQ(R::BadOffset, a.feed(1, 40, true, (const uint8_t*)p3, strlen(p3)));
}

TEST(SlotAssembly, ARepeatedChunkIsRefused) {
  SlotAssembly<240> a;
  const char* p = "Nokia:d=4,";
  ASSERT_EQ(R::Accepted, a.feed(1, 0, true, (const uint8_t*)p, strlen(p)));
  ASSERT_EQ(R::Accepted, a.feed(1, (uint8_t)strlen(p), true, (const uint8_t*)p, strlen(p)));
  // Same offset again: a duplicate delivery.
  EXPECT_EQ(R::BadOffset, a.feed(1, (uint8_t)strlen(p), true, (const uint8_t*)p, strlen(p)));
}

// Two clients, or one client changing its mind, must not interleave into one tune.
TEST(SlotAssembly, ADifferentSlotMidAssemblyIsRefused) {
  SlotAssembly<240> a;
  const char* p = "Nokia:d=4,";
  ASSERT_EQ(R::Accepted, a.feed(1, 0, true, (const uint8_t*)p, strlen(p)));
  EXPECT_EQ(R::BadSlot, a.feed(2, (uint8_t)strlen(p), true, (const uint8_t*)p, strlen(p)));
}

// Offset 0 always restarts, so a client that gave up halfway can simply begin again.
TEST(SlotAssembly, OffsetZeroRestartsCleanly) {
  SlotAssembly<240> a;
  const char* junk = "abandoned";
  ASSERT_EQ(R::Accepted, a.feed(1, 0, true, (const uint8_t*)junk, strlen(junk)));
  const char* s = "Tick:d=32,o=8,b=120:c";
  EXPECT_EQ(R::Complete, a.feed(4, 0, false, (const uint8_t*)s, strlen(s)));
  EXPECT_STREQ(s, a.text()) << "the abandoned bytes must not survive a restart";
  EXPECT_EQ(4u, a.slot());
}

TEST(SlotAssembly, RefusesPastTheLimitRatherThanTruncating) {
  SlotAssembly<240> a;
  const std::string big(200, 'c');
  ASSERT_EQ(R::Accepted, a.feed(0, 0, true, (const uint8_t*)big.data(), big.size()));
  const std::string more(100, 'c');
  EXPECT_EQ(R::TooLong, a.feed(0, 200, false, (const uint8_t*)more.data(), more.size()));
  EXPECT_EQ(0u, a.length()) << "a refused overflow must not leave a partial tune staged";
  EXPECT_FALSE(a.active());
}

// Exactly N must be accepted -- an off-by-one here would refuse the longest legal tune.
TEST(SlotAssembly, ExactlyTheLimitIsAccepted) {
  SlotAssembly<240> a;
  const std::string full(240, 'c');
  EXPECT_EQ(R::Complete, a.feed(0, 0, false, (const uint8_t*)full.data(), full.size()));
  EXPECT_EQ(240u, a.length());
}

// A slot may hold only what the editor could type, so both write routes agree (#1477).
TEST(SlotAssembly, RefusesBytesASlotMayNotHold) {
  for (uint8_t bad : {(uint8_t)0x00, (uint8_t)0x09, (uint8_t)0x0A, (uint8_t)0x1F,
                      (uint8_t)0x7F, (uint8_t)0x80, (uint8_t)0xFF}) {
    SlotAssembly<240> a;
    const uint8_t buf[] = {'c', bad, 'd'};
    EXPECT_EQ(R::BadByte, a.feed(0, 0, false, buf, sizeof(buf)))
        << "byte 0x" << std::hex << (int)bad << " should be refused";
    EXPECT_EQ(0u, a.length()) << "a refused chunk must not leave anything staged";
  }
}

TEST(SlotAssembly, PrintableEdgesAreAccepted) {
  SlotAssembly<240> a;
  const uint8_t buf[] = {0x20, 0x7E, '#', '=', ':', ',', '.'};
  EXPECT_EQ(R::Complete, a.feed(0, 0, false, buf, sizeof(buf)));
  EXPECT_EQ(sizeof(buf), a.length());
}

TEST(SlotAssembly, AnEmptyFinalChunkIsAnEmptyTune) {
  // How a client clears a slot: push nothing, finally. The store turns an empty string
  // into a clear, so this needs no separate sub-code.
  SlotAssembly<240> a;
  EXPECT_EQ(R::Complete, a.feed(5, 0, false, nullptr, 0));
  EXPECT_EQ(0u, a.length());
  EXPECT_STREQ("", a.text());
}

TEST(SlotAssembly, ResetClearsEverything) {
  SlotAssembly<240> a;
  const char* p = "Nokia:d=4,";
  a.feed(1, 0, true, (const uint8_t*)p, strlen(p));
  a.reset();
  EXPECT_EQ(0u, a.length());
  EXPECT_FALSE(a.active());
  EXPECT_STREQ("", a.text());
  // And a non-zero offset after a reset is refused rather than treated as a fresh start.
  EXPECT_EQ(R::BadSlot, a.feed(1, 5, true, (const uint8_t*)p, strlen(p)));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
