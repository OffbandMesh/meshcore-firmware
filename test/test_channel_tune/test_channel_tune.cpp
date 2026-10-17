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

// An unset channel inheriting an unset global. The event path always resolves `inherited`
// to a real tune today, so this is not reachable -- pinned because review of #1522 named
// it as the one resolution input the tests never used, and because the answer has to be
// "hand the sentinel straight back" rather than anything cleverer.
TEST(ChannelTuneResolution, AnUnsetChannelInheritingTheSentinelGetsTheSentinel) {
  EXPECT_EQ(kTuneUnset, resolveChannelTune(kChannelTuneUnset, kTuneUnset));
}

// ---- the spare field in /channels2 (#1523) -------------------------------------------

// The reason the field was free: an older file has zeros there, and zeros mean unset.
TEST(ChannelTuneStorage, ALegacyRecordsZeroesReadAsUnset) {
  const uint8_t legacy[kChannelSpareBytes] = {0, 0, 0, 0};
  EXPECT_EQ(kChannelTuneUnset, channelTuneFromSpare(legacy));
  EXPECT_EQ(kTuneUnset, decodeChannelTune(channelTuneFromSpare(legacy)));
}

TEST(ChannelTuneStorage, TheTuneRoundTripsThroughTheSpareField) {
  uint8_t spare[kChannelSpareBytes];
  for (uint8_t id = 0; id < (uint8_t)(kFirstSlotId + kSlotCount); id++) {
    channelTuneToSpare(encodeChannelTune(id), spare);
    EXPECT_EQ(id, decodeChannelTune(channelTuneFromSpare(spare)))
        << "id " << (int)id << " did not survive the file";
  }
}

TEST(ChannelTuneStorage, TheOtherThreeBytesStayZero) {
  uint8_t spare[kChannelSpareBytes];
  for (size_t i = 0; i < kChannelSpareBytes; i++) spare[i] = 0xAA;  // junk to overwrite
  channelTuneToSpare(encodeChannelTune(kTuneKnock), spare);
  EXPECT_EQ(encodeChannelTune(kTuneKnock), spare[kChannelTuneSpareIndex]);
  for (size_t i = 0; i < kChannelSpareBytes; i++) {
    if (i == kChannelTuneSpareIndex) continue;
    EXPECT_EQ(0, spare[i]) << "spare byte " << i << " must stay zero for the next field";
  }
}

// The property the whole "a downgrade loses nothing" argument rests on. #1522's
// resolution falls back for an id this build cannot read; that is only not data loss
// because the BYTE survives a save. So the persistence path has to be byte-preserving
// over the entire range -- including ids no build has ever defined -- and must not
// re-encode whatever it resolved.
TEST(ChannelTuneStorage, EveryByteSurvivesASaveUnchangedIncludingUnresolvableIds) {
  for (int b = 0; b <= 255; b++) {
    uint8_t spare[kChannelSpareBytes];
    channelTuneToSpare((uint8_t)b, spare);
    EXPECT_EQ(b, (int)channelTuneFromSpare(spare))
        << "stored byte " << b << " was altered by a save -- an unresolvable id must be "
        << "written back as it was read, or a re-upgrade cannot restore the choice";
  }
}

TEST(ChannelTuneStorage, ANullSpareFieldIsNotDereferenced) {
  EXPECT_EQ(kChannelTuneUnset, channelTuneFromSpare(nullptr));
  channelTuneToSpare(encodeChannelTune(kTuneChime), nullptr);  // must not crash
}

// ChannelDetails::tune is declared `uint8_t tune = 0;` and cannot include this header --
// BaseChatMesh.h pulls it into every role on every variant. That literal 0 is only
// correct while unset is 0, so the dependency is pinned here instead.
TEST(ChannelTuneStorage, ChannelDetailsDefaultOfZeroMeansUnset) {
  static_assert(kChannelTuneUnset == 0,
                "ChannelDetails::tune defaults to a literal 0 and means unset");
  EXPECT_EQ(kTuneUnset, decodeChannelTune(0));
}

// ---- an edit from the app (#1523) -----------------------------------------------------

// The bug this prevents: CMD_SET_CHANNEL carries only a name and a secret, so renaming a
// channel from the phone would otherwise clear the tune set on the badge.
TEST(ChannelTuneAfterEdit, ARenameKeepsTheTune) {
  const uint8_t stored = encodeChannelTune(kTuneKnock);
  EXPECT_EQ(stored, channelTuneAfterEdit(false, true, stored));
  // Including a choice of Silent, which is the one a careless "if (tune) keep it" would
  // drop -- Silent is stored as 1, not 0, exactly so it is not falsy.
  const uint8_t silent = encodeChannelTune(kTuneSilent);
  EXPECT_NE(kChannelTuneUnset, silent);
  EXPECT_EQ(silent, channelTuneAfterEdit(false, true, silent));
  // And an id this build cannot resolve: an edit must not normalise it away either.
  EXPECT_EQ(200, channelTuneAfterEdit(false, true, 200));
}

TEST(ChannelTuneAfterEdit, ANewlyJoinedSlotStartsUnset) {
  // Whatever happened to be in the slot's record before, a newly joined channel takes the
  // global tune rather than inheriting a stranger's choice.
  EXPECT_EQ(kChannelTuneUnset,
            channelTuneAfterEdit(true, false, encodeChannelTune(kTuneKnock)));
  EXPECT_EQ(kChannelTuneUnset, channelTuneAfterEdit(true, false, kChannelTuneUnset));
  // A caller that somehow reports an empty slot AND a matching channel is still a new
  // channel: emptiness wins, so the answer cannot depend on which flag is read first.
  EXPECT_EQ(kChannelTuneUnset,
            channelTuneAfterEdit(true, true, encodeChannelTune(kTuneKnock)));
}

// Review's finding, and it was right: channel slots are a fixed table the app reuses, so
// an occupied slot is not the same thing as the same channel. A tune chosen for a channel
// that has been replaced must not play for whatever took its place.
TEST(ChannelTuneAfterEdit, AReplacementDoesNotInheritTheOldChannelsTune) {
  EXPECT_EQ(kChannelTuneUnset,
            channelTuneAfterEdit(false, false, encodeChannelTune(kTuneKnock)));
  // A re-key of the "same" channel counts as a replacement too -- the secret is what
  // identifies a channel, and a new secret is a different channel on the air.
  EXPECT_EQ(kChannelTuneUnset,
            channelTuneAfterEdit(false, false, encodeChannelTune(kTuneSilent)));
}

// ---- the picker's rows (#1525) --------------------------------------------------------

// For an event there is no Default: an event always plays something, so there is nothing
// to fall back to and no way to un-set it. Rows are ids, one for one, exactly as the
// picker behaved before this mode existed.
TEST(TonePickerRows, AnEventsRowsAreTheIdsUnchanged) {
  EXPECT_EQ((int)kPickerIdEnd, tonePickerRowCount(false));
  for (uint8_t id = 0; id < kPickerIdEnd; id++) {
    EXPECT_EQ((int)id, tonePickerRowFor(false, id));
    EXPECT_EQ(id, tonePickerIdAt(false, (int)id));
  }
  EXPECT_EQ(-1, tonePickerRowFor(false, kTuneUnset)) << "an event cannot be un-set";
}

// A conversation gets one more row, and it is first. The owner's mockup puts Default at
// the top because it is the state a conversation starts in.
TEST(TonePickerRows, AConversationGetsADefaultRowFirst) {
  EXPECT_EQ((int)kPickerIdEnd + 1, tonePickerRowCount(true));
  EXPECT_EQ(0, kTonePickerDefaultRow);
  EXPECT_EQ(kTuneUnset, tonePickerIdAt(true, kTonePickerDefaultRow));
  EXPECT_EQ(kTonePickerDefaultRow, tonePickerRowFor(true, kTuneUnset));
}

// Every tune shifts down by one, and the mapping is a bijection. A one-off here would
// put the highlight on the wrong row or audition the wrong tune -- silently, since both
// ends are uint8_t and nothing would fail to compile.
TEST(TonePickerRows, TheConversationMappingRoundTripsEveryId) {
  for (uint8_t id = 0; id < kPickerIdEnd; id++) {
    const int row = tonePickerRowFor(true, id);
    EXPECT_EQ((int)id + 1, row) << "id " << (int)id << " sits on the wrong row";
    EXPECT_EQ(id, tonePickerIdAt(true, row)) << "row " << row << " offers the wrong id";
  }
  // And the Default row is not one of them.
  for (uint8_t id = 0; id < kPickerIdEnd; id++) {
    EXPECT_NE(kTonePickerDefaultRow, tonePickerRowFor(true, id));
  }
}

TEST(TonePickerRows, OutOfRangeRowsAndIdsAnswerSafely) {
  // A row past the end reads as unset rather than as an arbitrary tune, so a selection
  // left somewhere stale cannot audition or commit a tune nobody chose.
  EXPECT_EQ(kTuneUnset, tonePickerIdAt(false, (int)kPickerIdEnd));
  EXPECT_EQ(kTuneUnset, tonePickerIdAt(true, (int)kPickerIdEnd + 1));
  EXPECT_EQ(kTuneUnset, tonePickerIdAt(false, -1));
  EXPECT_EQ(kTuneUnset, tonePickerIdAt(true, -1));
  // An id the picker never shows has no row, in either mode.
  EXPECT_EQ(-1, tonePickerRowFor(false, kPickerIdEnd));
  EXPECT_EQ(-1, tonePickerRowFor(true, kPickerIdEnd));
  EXPECT_EQ(-1, tonePickerRowFor(false, 200));
  EXPECT_EQ(-1, tonePickerRowFor(true, 200));
}

// Choosing Default has to be the same operation as clearing the channel, not a second
// path that happens to agree -- the storage only has one way to say "no choice".
TEST(TonePickerRows, ChoosingDefaultClearsTheChannel) {
  const uint8_t chosen = tonePickerIdAt(true, kTonePickerDefaultRow);
  EXPECT_EQ(kChannelTuneUnset, encodeChannelTune(chosen));
  EXPECT_EQ(kTuneChime, resolveChannelTune(encodeChannelTune(chosen), kTuneChime));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
