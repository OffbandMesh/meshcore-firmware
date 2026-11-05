// Offband #606: per-channel notification mode -- the stored byte, the decision, and the
// carry-over when the app overwrites a slot.
//
// These run the REAL helpers from helpers/ChannelNotify.h. Nothing is re-implemented
// here: a test that encoded the rules itself would keep passing while the header was
// broken.
//
// The weight is on the NEGATIVE cases. This feature decides whether a radio makes a
// sound, and both failure directions are real complaints: a channel that will not go
// quiet is what started this work, and a channel that silently goes quiet is worse,
// because nobody reports a message they never heard about.

#include <gtest/gtest.h>

#include <helpers/ChannelNotify.h>
#include <helpers/ui/TuneSlots.h>

using namespace offband;

// ---------------------------------------------------------------- the stored byte

TEST(ChanNotifyStored, ZeroIsUnsetAndNotAll) {
  // The whole reason the encoding is 1-based. Every record written before this feature,
  // and every fresh channel, has zero here. If 0 meant All, adding this feature would
  // have set every channel in the field to All -- loudest, silently, on upgrade.
  EXPECT_EQ(0, kChanNotifyUnset);
  EXPECT_NE(kChanNotifyAll, kChanNotifyUnset);
  EXPECT_EQ(1, kChanNotifyAll);
}

TEST(ChanNotifyStored, SettableAcceptsOnlyTheFourValues) {
  for (uint8_t v = 0; v < kChanNotifyCount; v++) EXPECT_TRUE(channelNotifySettable(v)) << (int)v;
  for (int v : {4, 5, 7, 42, 255}) EXPECT_FALSE(channelNotifySettable((uint8_t)v)) << v;
}

// ---------------------------------------------------------------- the spare field

TEST(ChanNotifySpare, RoundTripsAndLeavesTheTuneAlone) {
  uint8_t spare[kChannelSpareBytes];
  for (uint8_t mode = 0; mode < kChanNotifyCount; mode++) {
    const uint8_t tune = encodeChannelTune(kTuneChime);
    channelSpareToFile(tune, mode, spare);
    EXPECT_EQ(mode, channelNotifyFromSpare(spare)) << "mode " << (int)mode;
    EXPECT_EQ(tune, channelTuneFromSpare(spare)) << "the tune was disturbed";
  }
}

TEST(ChanNotifySpare, TheTwoResidentsDoNotShareAByte) {
  EXPECT_NE(kChannelTuneSpareIndex, kChannelNotifySpareIndex);
  uint8_t spare[kChannelSpareBytes];
  channelSpareToFile(encodeChannelTune(kTuneKnock), kChanNotifyOff, spare);
  EXPECT_NE(spare[kChannelTuneSpareIndex], spare[kChannelNotifySpareIndex]);
}

TEST(ChanNotifySpare, TheRemainingBytesStayZero) {
  // #1523 zeroes them so a later field starts from a known value. #606 took one of the
  // three; the other two must still be zero, or whatever takes the next one inherits
  // this build's leftovers.
  uint8_t spare[kChannelSpareBytes];
  for (size_t i = 0; i < kChannelSpareBytes; i++) spare[i] = 0xAB;
  channelSpareToFile(encodeChannelTune(kTuneChime), kChanNotifyMentions, spare);
  for (size_t i = 0; i < kChannelSpareBytes; i++) {
    if (i == kChannelTuneSpareIndex || i == kChannelNotifySpareIndex) continue;
    EXPECT_EQ(0, spare[i]) << "spare byte " << i << " was left dirty";
  }
}

TEST(ChanNotifySpare, ALegacyRecordReadsAsUnset) {
  uint8_t legacy[kChannelSpareBytes] = {0, 0, 0, 0};   // written before either feature
  EXPECT_EQ(kChanNotifyUnset, channelNotifyFromSpare(legacy));
}

TEST(ChanNotifySpare, AnUnreadableByteReadsAsUnsetRatherThanBeingTrusted) {
  // Deliberately UNLIKE the tune, which preserves an unresolvable id byte-for-byte
  // because a later build may know it. The mode space is closed at four values and will
  // not grow a meaning later, so there is nothing to preserve -- and a corrupt byte must
  // not decide whether a radio sounds.
  uint8_t spare[kChannelSpareBytes] = {0, 0, 0, 0};
  for (int junk : {4, 9, 200, 255}) {
    spare[kChannelNotifySpareIndex] = (uint8_t)junk;
    EXPECT_EQ(kChanNotifyUnset, channelNotifyFromSpare(spare)) << "stored " << junk;
  }
}

TEST(ChanNotifySpare, NullIsSafeBothWays) {
  EXPECT_EQ(kChanNotifyUnset, channelNotifyFromSpare(nullptr));
  channelSpareToFile(0, kChanNotifyOff, nullptr);   // must not crash
}

// ---------------------------------------------------------------- the decision

TEST(ChanNotifyResolve, ASetChannelDecidesForItself) {
  // THE point of the feature. A client writes a channel's mode and the radio obeys it --
  // there is no second switch to find and enable first. An earlier draft made the scope a
  // master switch that ignored these unless a fourth "Inherit" scope was selected, which
  // meant setting a channel to Off did nothing until the user discovered a separate
  // device setting. That is the complaint this work exists to end, so it is asserted.
  for (uint8_t scope : {kScopeAll, kScopeSelf}) {
    EXPECT_EQ(kChanNotifyAll,      resolveChannelNotify(scope, kChanNotifyAll))      << (int)scope;
    EXPECT_EQ(kChanNotifyMentions, resolveChannelNotify(scope, kChanNotifyMentions)) << (int)scope;
    EXPECT_EQ(kChanNotifyOff,      resolveChannelNotify(scope, kChanNotifyOff))      << (int)scope;
  }
}

TEST(ChanNotifyResolve, AnUnsetChannelFallsBackToTheScope) {
  EXPECT_EQ(kChanNotifyAll,      resolveChannelNotify(kScopeAll,  kChanNotifyUnset));
  EXPECT_EQ(kChanNotifyMentions, resolveChannelNotify(kScopeSelf, kChanNotifyUnset));
  EXPECT_EQ(kChanNotifyOff,      resolveChannelNotify(kScopeNone, kChanNotifyUnset));
}

TEST(ChanNotifyResolve, NoneIsAbsoluteAndOverridesASetChannel) {
  // The one exception to "a set channel decides". A user who sets the device to Off is
  // asking for silence, not a negotiation -- a channel still sounding through it is the
  // worst failure this feature has.
  for (uint8_t stored = 0; stored < kChanNotifyCount; stored++) {
    EXPECT_EQ(kChanNotifyOff, resolveChannelNotify(kScopeNone, stored)) << (int)stored;
  }
}

TEST(ChanNotifyResolve, AJunkStoredByteFallsBackToTheScopeNotToSilence) {
  for (int junk : {4, 9, 200, 255}) {
    EXPECT_EQ(kChanNotifyAll,      resolveChannelNotify(kScopeAll,  (uint8_t)junk)) << junk;
    EXPECT_EQ(kChanNotifyMentions, resolveChannelNotify(kScopeSelf, (uint8_t)junk)) << junk;
  }
}

TEST(ChanNotifyResolve, NeverReturnsUnset) {
  // The caller gets a decision, not a lookup -- there is no third state to handle at the
  // alert site.
  for (uint8_t scope = 0; scope < 8; scope++) {
    for (uint8_t stored = 0; stored < 8; stored++) {
      EXPECT_NE(kChanNotifyUnset, resolveChannelNotify(scope, stored))
          << "scope " << (int)scope << " stored " << (int)stored;
    }
  }
}

TEST(ChanNotifyResolve, AnUnknownScopeFallsBackToAllForAnUnsetChannel) {
  for (int junk : {3, 7, 99, 255}) {
    EXPECT_EQ(kChanNotifyAll, resolveChannelNotify((uint8_t)junk, kChanNotifyUnset)) << junk;
  }
}

TEST(ChanNotifyResolve, ThereIsNoInheritScope) {
  // Guards the category error this feature first made: "inherit" is a CLIENT concept --
  // a channel row in the app that follows the device setting rather than carrying its
  // own. The firmware is the authority and has nothing to inherit from; the device says
  // the same thing with a per-channel byte that is not set. If a fourth scope value ever
  // appears, NOTIFY_SCOPE_COUNT moves and this fails.
  EXPECT_EQ(3, (int)kScopeNone + 1) << "a scope was added; unset is how a channel defers";
}

// ---------------------------------------------------------------- alerting

TEST(ChanNotifyAlerts, TheFullMatrix) {
  EXPECT_TRUE(channelNotifyAlerts(kChanNotifyAll, false));
  EXPECT_TRUE(channelNotifyAlerts(kChanNotifyAll, true));
  EXPECT_FALSE(channelNotifyAlerts(kChanNotifyMentions, false));
  EXPECT_TRUE(channelNotifyAlerts(kChanNotifyMentions, true));
  EXPECT_FALSE(channelNotifyAlerts(kChanNotifyOff, false));
  EXPECT_FALSE(channelNotifyAlerts(kChanNotifyOff, true));
}

TEST(ChanNotifyAlerts, OffBeatsAMention) {
  // A channel set to Off stays silent even when the message names you. Asserted on its
  // own because the opposite is a plausible reading of "mentions always get through",
  // and it is not what Off means.
  EXPECT_FALSE(channelNotifyAlerts(resolveChannelNotify(kScopeAll, kChanNotifyOff), true));
}

TEST(ChanNotifyAlerts, AChannelSetToAllEscapesADeviceWideSelf) {
  // The composition stated through the alert. The device default is SELF, so this is the
  // case a user hits when they want ONE channel audible on an otherwise quiet radio:
  // setting that channel to All must actually make it sound, not be filtered by the
  // default it was meant to escape.
  const uint8_t resolved = resolveChannelNotify(kScopeSelf, kChanNotifyAll);
  EXPECT_TRUE(channelNotifyAlerts(resolved, false));
  EXPECT_TRUE(channelNotifyAlerts(resolved, true));
}

TEST(ChanNotifyAlerts, TheDeviceDefaultIsMentionsOnlyForAnUntouchedChannel) {
  // #606: the default scope is SELF, not upstream's ALL -- a radio in a pocket beeping
  // for nearly every channel message is the wrong default. An untouched channel is
  // therefore quiet unless it names you.
  const uint8_t resolved = resolveChannelNotify(kScopeSelf, kChanNotifyUnset);
  EXPECT_FALSE(channelNotifyAlerts(resolved, false));
  EXPECT_TRUE(channelNotifyAlerts(resolved, true));
}

// ---------------------------------------------------------------- the carry-over

TEST(ChanNotifyAfterEdit, KeptThroughARename) {
  // What people actually do. Renaming a channel from the phone must not clear the mode
  // set on the device.
  EXPECT_EQ(kChanNotifyOff, channelNotifyAfterEdit(false, true, kChanNotifyOff));
  EXPECT_EQ(kChanNotifyMentions, channelNotifyAfterEdit(false, true, kChanNotifyMentions));
}

TEST(ChanNotifyAfterEdit, DroppedAcrossAReplacement) {
  // THE case this function exists for. Slots are a fixed table the app reuses, so "the
  // slot was not empty" is also true when channel A has been replaced by channel B --
  // and B must not inherit a mode chosen for a channel that is gone.
  EXPECT_EQ(kChanNotifyUnset, channelNotifyAfterEdit(false, false, kChanNotifyOff));
}

TEST(ChanNotifyAfterEdit, UnsetOnAFreshSlot) {
  EXPECT_EQ(kChanNotifyUnset, channelNotifyAfterEdit(true, false, kChanNotifyOff));
  // was_empty wins: a fresh slot cannot also be "the same channel", and if a caller ever
  // passes both, inheriting a stale mode is the worse answer.
  EXPECT_EQ(kChanNotifyUnset, channelNotifyAfterEdit(true, true, kChanNotifyOff));
}

TEST(ChanNotifyAfterEdit, MatchesTheTunesRuleNotTheRetiredGuiArrays) {
  // TuneSlots.h records that the GUI's per-channel array (#1457) keyed off occupancy
  // alone and so DID carry a mode across a replacement. #606 takes the tune's stricter
  // rule instead, which is a deliberate behaviour change for GUI boards. Asserted so the
  // looser rule cannot quietly come back with the GUI carry-over.
  for (uint8_t before = 1; before < kChanNotifyCount; before++) {
    EXPECT_EQ(channelTuneAfterEdit(false, false, before) == 0,
              channelNotifyAfterEdit(false, false, before) == kChanNotifyUnset)
        << "notify and tune disagree on a replacement, before=" << (int)before;
  }
}

// ------------------------------------------------- the badge's own cycle

TEST(ChanNotifyCycle, VisitsEveryModeAndReturns) {
  // A cycle that skipped a value would make it unreachable from the badge entirely.
  uint8_t m = kChanNotifyUnset;
  std::set<uint8_t> seen;
  for (int i = 0; i < kChanNotifyCount; i++) {
    seen.insert(m);
    m = nextChannelNotify(m);
  }
  EXPECT_EQ((size_t)kChanNotifyCount, seen.size()) << "a mode is unreachable from the badge";
  EXPECT_EQ(kChanNotifyUnset, m) << "the cycle must close";
}

TEST(ChanNotifyCycle, DefaultIsReachable) {
  // THE case this exists for. Without UNSET in the cycle a user who has touched a channel
  // could never hand it back to the device default from the badge -- a one-way door, with
  // the way back available only from the client.
  bool found = false;
  uint8_t m = kChanNotifyAll;
  for (int i = 0; i < kChanNotifyCount; i++) {
    m = nextChannelNotify(m);
    if (m == kChanNotifyUnset) found = true;
  }
  EXPECT_TRUE(found) << "there is no way back to following the device scope";
}

TEST(ChanNotifyCycle, JunkLandsOnTheDefault) {
  // A byte outside the known set must resolve to something a user can then cycle out of,
  // not stick or wrap into another junk value.
  EXPECT_EQ(kChanNotifyUnset, nextChannelNotify(0xFF));
  EXPECT_EQ(kChanNotifyUnset, nextChannelNotify(kChanNotifyCount));
}

TEST(ChanNotifyLabel, EveryModeHasAShortLabel) {
  for (uint8_t m = 0; m < kChanNotifyCount; m++) {
    const char* s = channelNotifyLabel(m);
    ASSERT_NE(nullptr, s);
    EXPECT_GT(strlen(s), 0u) << "mode " << (int)m << " has no label";
    EXPECT_LE(strlen(s), 8u) << "label '" << s << "' will clip on the badge's note line";
  }
}

TEST(ChanNotifyLabel, UnsetReadsAsDefaultNotAsInherit) {
  // "Inherit" is the CLIENT's word for a row that follows the device. The device IS the
  // authority and has nothing to inherit from, so the badge says Default.
  EXPECT_STREQ("Default", channelNotifyLabel(kChanNotifyUnset));
  EXPECT_STREQ("Default", channelNotifyLabel(0xFF)) << "junk must read as the default too";
}

// ------------------------------------------------- what Public carries across slot 0

TEST(ChanNotifyCarry, PublicKeepsItsModeAndItsTuneAcrossTheMove) {
  // THE regression. placePublicInSlot0 rebuilds Public's record rather than moving it,
  // and before this helper existed it copied the tune and dropped the notify byte -- so
  // a user who silenced Public heard it again at the next boot that re-placed slot 0.
  // Public cannot be deleted, so this is the one channel where a lost mode is permanent
  // until the user finds the setting again.
  //
  // Both outputs are pre-loaded with a value the carry must overwrite, so a helper that
  // wrote neither would fail here rather than pass on whatever the caller already had.
  uint8_t tune = 99, notify = 99;
  carryChannelPrefs(true, 5, kChanNotifyOff, &tune, &notify);
  EXPECT_EQ(5u, tune);
  EXPECT_EQ(kChanNotifyOff, notify);
}

TEST(ChanNotifyCarry, NoPriorPublicCarriesNothing) {
  // The negative half: with no prior Public there is nothing to carry, and both fields
  // must come out UNSET rather than an invented All. Unset means the channel takes the
  // device scope, which is the only answer that is not a guess about what the user wants.
  uint8_t tune = 99, notify = 99;
  carryChannelPrefs(false, 5, kChanNotifyOff, &tune, &notify);
  EXPECT_EQ(kChannelTuneUnset, tune);
  EXPECT_EQ(kChanNotifyUnset, notify);
}

TEST(ChanNotifyCarry, EveryModeSurvivesTheMoveUnchanged) {
  // Walked across the whole space rather than spot-checked: a carry that mapped one mode
  // wrong would be a channel that changes how it sounds at a boot the user did not cause.
  for (uint8_t m = 0; m < kChanNotifyCount; m++) {
    uint8_t tune = 0, notify = 0xFF;
    carryChannelPrefs(true, kChannelTuneUnset, m, &tune, &notify);
    EXPECT_EQ(m, notify) << "mode " << (int)m << " did not survive the slot-0 move";
  }
}

TEST(ChanNotifyCarry, EveryTuneSurvivesTheMoveUnchanged) {
  // Symmetric coverage for the other resident, and not redundant: the tune is carried
  // BYTE-FOR-BYTE, including ids this build cannot resolve (TuneSlots.h keeps an unknown
  // id rather than clearing it, because a later build may know it). So the boundaries are
  // the interesting part -- a carry that validated or clamped the tune would silently
  // rewrite a channel's sound on a board that merely has an older tune table.
  const uint8_t tunes[] = {kChannelTuneUnset, 1, 7, 0x7F, 0xFE, 0xFF};
  for (uint8_t t : tunes) {
    uint8_t tune = 0xAA, notify = 0;
    carryChannelPrefs(true, t, kChanNotifyAll, &tune, &notify);
    EXPECT_EQ(t, tune) << "tune " << (int)t << " did not survive the slot-0 move";
  }
}

TEST(ChanNotifyCarry, NeitherOutputIsWrittenThroughANullPointer) {
  // The guard, matching channelSpareToFile's. A call site that passes one field and not
  // the other is a programming error, and the helper must not write a partial carry --
  // half a carry is worse than none, because the record then disagrees with itself.
  uint8_t only = 42;
  carryChannelPrefs(true, 5, kChanNotifyOff, &only, nullptr);
  EXPECT_EQ(42u, only) << "a partial carry was written when one output was null";
  carryChannelPrefs(true, 5, kChanNotifyOff, nullptr, &only);
  EXPECT_EQ(42u, only) << "a partial carry was written when one output was null";
  carryChannelPrefs(true, 5, kChanNotifyOff, nullptr, nullptr);   // must not crash
}

// ---------------------------------------------------------------- the record on disk

TEST(ChanNotifyRecord, TheChannelRecordStrideIsUnchanged) {
  // The whole reason this feature costs no migration. DataStore::hasChannels hardcodes
  // 4 + 32 + 32; if the spare field ever grew, that arithmetic and every stored file
  // would part company silently.
  EXPECT_EQ(4u, kChannelSpareBytes);
  EXPECT_EQ(68u, kChannelSpareBytes + 32 + 32);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
