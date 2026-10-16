// Native unit tests for the notification-scope rules (#1468).
//
// notify_scope is the gate UPSTREAM of every tune choice: channelMsgShouldNotify() and
// the DM path consult it before notify() is called at all. A badge sitting on SELF makes
// no sound on channel traffic however its tunes are set -- which is exactly how a silent
// badge was diagnosed on the bench, from 30 `[mention]` log lines and zero notify lines.
//
// These three rules were pulled out of the screen so they are testable here, and so the
// badge control and the 0xC5 client handler cannot drift apart. The screen itself needs a
// display and is verified on hardware.

#include <gtest/gtest.h>
#include <cstring>
#include <set>
#include <string>

#include "../../examples/companion_radio/NodePrefs.h"

using offband::nextNotifyScope;
using offband::notifyScopeLabel;
using offband::quietForScope;

// The order is a contract: it is what #510 documented for the triple-press on ui-orig
// and what a client's control follows. If it ever changes, it changes in both places or
// the same gesture means different things on different boards.
TEST(NotifyScopeCycle, FollowsTheDocumentedOrder) {
  EXPECT_EQ(NOTIFY_SCOPE_SELF, nextNotifyScope(NOTIFY_SCOPE_ALL));
  EXPECT_EQ(NOTIFY_SCOPE_NONE, nextNotifyScope(NOTIFY_SCOPE_SELF));
  EXPECT_EQ(NOTIFY_SCOPE_ALL,  nextNotifyScope(NOTIFY_SCOPE_NONE));
}

// Cycling must visit every value and come home, or some setting is unreachable from the
// badge -- which is the defect this whole issue is about.
TEST(NotifyScopeCycle, VisitsEveryValueAndReturns) {
  std::set<int> seen;
  uint8_t s = NOTIFY_SCOPE_ALL;
  for (int i = 0; i < NOTIFY_SCOPE_COUNT; i++) {
    seen.insert(s);
    s = nextNotifyScope(s);
  }
  EXPECT_EQ((size_t)NOTIFY_SCOPE_COUNT, seen.size()) << "a scope cannot be reached by cycling";
  EXPECT_EQ(NOTIFY_SCOPE_ALL, s) << "the cycle does not return to its start";
}

// A stored value from a longer table (a downgrade) or a corrupt byte must not strand the
// control: it has to cycle to something real rather than wrap to another junk value.
TEST(NotifyScopeCycle, UnknownValuesRejoinTheCycle) {
  for (int junk : {NOTIFY_SCOPE_COUNT, 7, 42, 255}) {
    EXPECT_EQ(NOTIFY_SCOPE_ALL, nextNotifyScope((uint8_t)junk))
        << "stored " << junk << " would not cycle back into range";
  }
}

// THE regression this file exists for. The 0xC5 SET handler sets buzzer_quiet from the
// scope; a badge control that did not would move the gate and leave the driver muted --
// the exact one-way strand that left a badge silent with no route back.
TEST(NotifyScopeQuiet, OnlyNoneIsQuiet) {
  EXPECT_EQ(0, quietForScope(NOTIFY_SCOPE_ALL));
  EXPECT_EQ(0, quietForScope(NOTIFY_SCOPE_SELF));
  EXPECT_EQ(1, quietForScope(NOTIFY_SCOPE_NONE));
}

// Leaving NONE by any route must un-mute. Stated as its own test because "entering NONE
// mutes" was already true and was never the bug.
TEST(NotifyScopeQuiet, LeavingNoneUnmutes) {
  const uint8_t after = nextNotifyScope(NOTIFY_SCOPE_NONE);
  EXPECT_EQ(0, quietForScope(after)) << "cycling off Off leaves the buzzer muted";
}

// The middle value filters channel traffic only -- a DM still sounds at SELF. So the
// label must not say "Mentions", which would claim DMs are filtered too. This asserts
// the promise rather than the spelling: whatever the wording, it may not be the bare
// word the client uses for its per-channel filter (#1469).
TEST(NotifyScopeLabel, TheMiddleValueDoesNotClaimDmsAreFiltered) {
  const std::string self = notifyScopeLabel(NOTIFY_SCOPE_SELF);
  EXPECT_NE("Mentions", self) << "at SELF a DM still sounds; this label denies it";
  EXPECT_NE(std::string::npos, self.find("DM"))
      << "'" << self << "' does not tell the user DMs are unaffected";
}

TEST(NotifyScopeLabel, EveryValueHasItsOwnShortLabel) {
  std::set<std::string> labels;
  for (uint8_t s = 0; s < NOTIFY_SCOPE_COUNT; s++) {
    const char* l = notifyScopeLabel(s);
    ASSERT_NE(nullptr, l);
    EXPECT_GT(strlen(l), 0u);
    // The Tones row is label + value on one 21-column line; "New contact" is the widest
    // label at 11, so a value over 9 cannot fit beside it.
    EXPECT_LE(strlen(l), 9u) << "'" << l << "' is too wide for the Tones row";
    labels.insert(l);
  }
  EXPECT_EQ((size_t)NOTIFY_SCOPE_COUNT, labels.size()) << "two scopes read the same";
}

// An out-of-range value must still render as something, not blank or garbage.
TEST(NotifyScopeLabel, UnknownValuesRenderAsTheFallback) {
  for (int junk : {NOTIFY_SCOPE_COUNT, 99, 255}) {
    EXPECT_STREQ(notifyScopeLabel(NOTIFY_SCOPE_ALL), notifyScopeLabel((uint8_t)junk));
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
