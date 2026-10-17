// Native unit tests for a tune set on one channel (#1522).
//
// The per-channel tune lives in a byte of /channels2 that has only ever held zero. The
// encoding is id + 1, and the reason is the first test below: tune id 0 is Silent, a real
// choice, so a stored 0 meaning Silent would have taken every channel on every existing
// badge to silence the moment it ran this build -- from a field that was only padding.
//
// These are the rules that decide what a channel sounds like. The storage and the screens
// are separate tasks; what is testable here is the meaning.

#include <gtest/gtest.h>
#include <set>

#include <helpers/ui/TuneSlots.h>

using namespace offband;

// ---- the reason the encoding exists --------------------------------------------------

// The whole point. A legacy file is all zeros in that byte; those must read as "no choice
// made", NOT as a choice of Silent.
TEST(ChannelTuneEncoding, ALegacyZeroMeansUnsetAndNotSilent) {
  EXPECT_EQ(kTuneUnset, decodeChannelTune(0))
      << "a byte that was only ever padding would otherwise silence every channel";
  EXPECT_NE(kTuneSilent, decodeChannelTune(0));
}

// And Silent is still storable -- it is a real choice, it just is not zero.
TEST(ChannelTuneEncoding, SilentIsStorableAsOne) {
  EXPECT_EQ(1, encodeChannelTune(kTuneSilent));
  EXPECT_EQ(kTuneSilent, decodeChannelTune(encodeChannelTune(kTuneSilent)));
}

TEST(ChannelTuneEncoding, EveryBuiltinAndSlotRoundTrips) {
  std::set<int> stored;
  const uint8_t kIdEnd = (uint8_t)(kFirstSlotId + kSlotCount);
  for (uint8_t id = 0; id < kIdEnd; id++) {
    const uint8_t s = encodeChannelTune(id);
    EXPECT_NE(kChannelTuneUnset, s) << "id " << (int)id << " must be storable";
    EXPECT_EQ(id, decodeChannelTune(s)) << "id " << (int)id << " did not round-trip";
    stored.insert(s);
  }
  EXPECT_EQ((size_t)kIdEnd, stored.size()) << "two ids share a stored value";
}

// kTuneUnset is 0xFF, and 0xFF + 1 wraps to 0 -- which happens to be the right answer.
// Asserted explicitly so it rests on the guard in encodeChannelTune rather than on
// unsigned overflow being convenient.
TEST(ChannelTuneEncoding, TheUnsetSentinelStoresAsUnsetWithoutRelyingOnWraparound) {
  EXPECT_EQ(kChannelTuneUnset, encodeChannelTune(kTuneUnset));
  EXPECT_EQ(kChannelTuneUnset, encodeChannelTune(255));
  EXPECT_EQ(kTuneUnset, decodeChannelTune(encodeChannelTune(kTuneUnset)));
}

TEST(ChannelTuneEncoding, TheTopOfTheStorableRangeIsExact) {
  EXPECT_EQ(255, encodeChannelTune(kMaxStorableTuneId));
  EXPECT_EQ(kMaxStorableTuneId, decodeChannelTune(255));
  EXPECT_EQ(kChannelTuneUnset, encodeChannelTune((uint8_t)(kMaxStorableTuneId + 1)));
}

// ---- the resolution order ------------------------------------------------------------

// An unset channel must behave EXACTLY as it did before this feature existed: whatever
// the event path already resolved.
TEST(ChannelTuneResolution, UnsetInheritsAndChangesNothing) {
  EXPECT_EQ(kTuneKerplop, resolveChannelTune(kChannelTuneUnset, kTuneKerplop));
  EXPECT_EQ(kTuneChime, resolveChannelTune(kChannelTuneUnset, kTuneChime));
  // Including when the inherited value is itself Silent.
  EXPECT_EQ(kTuneSilent, resolveChannelTune(kChannelTuneUnset, kTuneSilent));
}

TEST(ChannelTuneResolution, AChannelsOwnChoiceWins) {
  EXPECT_EQ(kTuneKnock, resolveChannelTune(encodeChannelTune(kTuneKnock), kTuneKerplop));
  // Silence chosen ON THE CHANNEL beats an audible inherited tune -- that is the point of
  // being able to choose it.
  EXPECT_EQ(kTuneSilent, resolveChannelTune(encodeChannelTune(kTuneSilent), kTuneChime));
}

TEST(ChannelTuneResolution, ASlotIdResolvesLikeAnyOtherTune) {
  const uint8_t slot = slotIdAt(2);
  EXPECT_EQ(slot, resolveChannelTune(encodeChannelTune(slot), kTuneChime));
}

// A downgrade, or a corrupt byte. It must fall back, and specifically NOT to silence: a
// channel going quiet because of a downgrade would be indistinguishable from someone
// having chosen Silent on it.
TEST(ChannelTuneResolution, AnUnresolvableIdFallsBackRatherThanGoingQuiet) {
  for (int junk : {(int)(kFirstSlotId + kSlotCount), 100, 200, (int)kMaxStorableTuneId}) {
    const uint8_t stored = encodeChannelTune((uint8_t)junk);
    EXPECT_EQ(kTuneChime, resolveChannelTune(stored, kTuneChime))
        << "stored id " << junk << " should inherit";
    EXPECT_NE(kTuneSilent, resolveChannelTune(stored, kTuneChime))
        << "stored id " << junk << " must not be mistaken for a choice of silence";
  }
}

// The un-listed trigger tune (#1422) is a builtin, so it resolves -- but nothing in the
// UI can choose it, which is what keeps it hidden. Pinned so a later change cannot make
// a channel silently able to hold it without that being a decision.
TEST(ChannelTuneResolution, TheUnlistedTuneIsStorableButNotOfferable) {
  EXPECT_TRUE(isBuiltinTune(kTuneTrigger));
  EXPECT_EQ(kTuneTrigger, resolveChannelTune(encodeChannelTune(kTuneTrigger), kTuneChime));
  EXPECT_EQ(nullptr, tuneName(kTuneTrigger)) << "it stopped being un-listed";
}

// Review called the static_assert insufficient, and it is right about what an assert can
// express: it checks that the current id ranges FIT the encoding, not that everything
// storable also RESOLVES. A third category of ids added without teaching
// resolveChannelTune about it would store fine and silently fall back.
//
// That contract is testable even though it is not assertable: everything the picker can
// offer must resolve to itself, never to the inherited tune.
TEST(ChannelTuneResolution, EveryOfferableIdResolvesToItself) {
  const uint8_t kIdEnd = (uint8_t)(kFirstSlotId + kSlotCount);
  // An inherited value deliberately different from every id under test, so a fallback
  // cannot pass by coincidence.
  const uint8_t sentinel = kTuneSilent;
  for (uint8_t id = 0; id < kIdEnd; id++) {
    if (id == sentinel) continue;
    EXPECT_EQ(id, resolveChannelTune(encodeChannelTune(id), sentinel))
        << "id " << (int)id << " is storable but does not resolve -- a new id range was "
        << "added without teaching resolveChannelTune about it";
  }
}

// Passing the unset sentinel is the CLEAR operation, which is how "Default" works. Pinned
// so nobody later makes it an error and breaks un-setting.
TEST(ChannelTuneEncoding, EncodingTheUnsetSentinelIsHowAChannelIsCleared) {
  EXPECT_EQ(kChannelTuneUnset, encodeChannelTune(kTuneUnset));
  EXPECT_EQ(kTuneChime, resolveChannelTune(encodeChannelTune(kTuneUnset), kTuneChime))
      << "clearing a channel must hand it back to the inherited tune";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
