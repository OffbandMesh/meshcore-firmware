// Native unit tests for the event-to-pref slot table (#1421).
//
// This closes a gap flagged twice during review and declined twice, on the grounds that
// NodePrefs was not reachable from the native env. It is: a RELATIVE include reaches it,
// `ConfigSerializer.cpp` is already in the native build_src_filter, and
// test_config_serializer already subclasses it on the host. No change to the shared env
// was needed, which was the whole reason the gap was left open.
//
// The regression this exists to catch is the one review described concretely: a
// copy-paste slip that points two events at the same pref member, or leaves one
// unmapped. Either makes a user's choice silently do nothing or quietly change another
// event's tune.

#include <gtest/gtest.h>
#include <cstring>
#include <set>
#include <string>

#include "../../examples/companion_radio/ToneSlots.h"

using namespace offband;

namespace {
int eventCount() { return (int)ToneEvent::Count; }
}  // namespace

// Every event has a slot, and `Count` and beyond have none.
TEST(ToneSlots, EveryEventHasAMemberAndTheSentinelsDoNot) {
  for (int i = 0; i < eventCount(); i++) {
    EXPECT_NE(nullptr, toneMember((ToneEvent)i)) << "event " << i << " has no pref slot";
  }
  EXPECT_EQ(nullptr, toneMember(ToneEvent::Count));
  EXPECT_EQ(nullptr, toneMember((ToneEvent)200));
}

// THE one this test exists for. Two events pointing at the same member is a copy-paste
// slip that no build would complain about: setting one event's tune would silently
// change another's, and nothing in the UI would hint at it.
TEST(ToneSlots, NoTwoEventsShareAMember) {
  std::set<std::string> seen;
  for (int i = 0; i < eventCount(); i++) {
    uint8_t NodePrefs::*const m = toneMember((ToneEvent)i);
    ASSERT_NE(nullptr, m);
    // Pointer-to-member is not printable, so compare by bytes.
    std::string key((const char*)&m, sizeof(m));
    EXPECT_TRUE(seen.insert(key).second) << "event " << i << " shares a slot with another";
  }
  EXPECT_EQ((size_t)eventCount(), seen.size());
}

// Stronger than comparing pointers: write a distinct value through every event and
// confirm each reads back its own. This catches aliasing a pointer comparison could miss.
TEST(ToneSlots, EachEventRoundTripsItsOwnValue) {
  NodePrefs p;
  for (int i = 0; i < eventCount(); i++) {
    ASSERT_TRUE(setStoredTone(&p, (ToneEvent)i, (uint8_t)(10 + i)));
  }
  for (int i = 0; i < eventCount(); i++) {
    EXPECT_EQ((uint8_t)(10 + i), storedTone(&p, (ToneEvent)i)) << "event " << i;
  }
}

// A fresh NodePrefs has chosen nothing, so every event reads unset -- which is what
// makes an un-configured badge follow the firmware's defaults.
TEST(ToneSlots, AFreshPrefsHasChosenNothing) {
  NodePrefs p;
  for (int i = 0; i < eventCount(); i++) {
    EXPECT_EQ(kTuneUnset, storedTone(&p, (ToneEvent)i)) << "event " << i;
    EXPECT_EQ(defaultTuneFor((ToneEvent)i), effectiveTone(&p, (ToneEvent)i));
  }
}

// A stored choice beats the default, and storing Silent is a choice rather than an
// absence -- the distinction the 0xFF sentinel exists for.
TEST(ToneSlots, AStoredChoiceWinsAndSilenceIsAChoice) {
  NodePrefs p;
  ASSERT_TRUE(setStoredTone(&p, ToneEvent::ContactMessage, kTuneKnock));
  EXPECT_EQ(kTuneKnock, effectiveTone(&p, ToneEvent::ContactMessage));

  ASSERT_TRUE(setStoredTone(&p, ToneEvent::ContactMessage, kTuneSilent));
  EXPECT_EQ(kTuneSilent, effectiveTone(&p, ToneEvent::ContactMessage))
      << "a deliberately silenced event must not fall back to the default";
  EXPECT_EQ(nullptr, tuneRtttl(effectiveTone(&p, ToneEvent::ContactMessage)));

  // And the other events are untouched by that write.
  EXPECT_EQ(defaultTuneFor(ToneEvent::Ack), effectiveTone(&p, ToneEvent::Ack));
}

// Null prefs and a slotless event both land on the compiled default rather than on
// silence, which is the behavior the firmware had before prefs existed.
TEST(ToneSlots, NoPrefsFallsBackRatherThanGoingSilent) {
  EXPECT_EQ(kTuneUnset, storedTone(nullptr, ToneEvent::ContactMessage));
  EXPECT_EQ(defaultTuneFor(ToneEvent::ContactMessage),
            effectiveTone(nullptr, ToneEvent::ContactMessage));
  EXPECT_FALSE(setStoredTone(nullptr, ToneEvent::ContactMessage, kTuneChime));

  NodePrefs p;
  EXPECT_FALSE(setStoredTone(&p, ToneEvent::Count, kTuneChime))
      << "writing a slotless event must report failure, not pretend to save";
}

// The screen's row list must cover every event exactly once, or the UI hides one with
// no way to reach it.
TEST(ToneRows, EveryEventHasExactlyOneRow) {
  ASSERT_EQ(eventCount(), kToneEventRowCount);
  std::set<int> seen;
  for (int i = 0; i < kToneEventRowCount; i++) {
    const ToneEventRow& r = kToneEventRows[i];
    EXPECT_TRUE(seen.insert((int)r.event).second) << "event listed twice at row " << i;
    ASSERT_NE(nullptr, r.label);
    EXPECT_GT(strlen(r.label), 0u) << "row " << i << " has no label";
    // A badge row is 21 columns at the large face, and a tune name shares it, so a
    // label much over a dozen characters leaves nothing for the value.
    EXPECT_LE(strlen(r.label), 12u) << "'" << r.label << "' crowds out the tune name";
  }
  EXPECT_EQ((size_t)eventCount(), seen.size());
}

// The words the owner approved, in the order drawn in the mockup he answered.
TEST(ToneRows, TheApprovedLabelsInTheApprovedOrder) {
  const char* expected[] = {"DM", "Channel", "Room", "New contact", "Ack", "Sent"};
  ASSERT_EQ(sizeof(expected) / sizeof(expected[0]), (size_t)kToneEventRowCount);
  for (int i = 0; i < kToneEventRowCount; i++) {
    EXPECT_STREQ(expected[i], kToneEventRows[i].label) << "row " << i << " moved";
  }
}

// The gap the PR-level integration gate found, and it is a real one: the
// static_assert in ToneSlots.h compares the slot array's LENGTH to ToneEvent::Count,
// which catches a missing or extra slot and says nothing about ORDER. Reorder the enum
// without reordering the array -- a plausible tidy-up, grouping the two message events
// together, say -- and the assert still passes, the existing tests still pass, and every
// event quietly writes another event's pref. The UI would show one tune and the badge
// would play another, with nothing failing anywhere.
//
// Nor would the compiler help: arduino_base's build_flags begin with `-w`, so there is no
// -Wswitch and no -Werror anywhere in this tree. A test is the only enforcement available.
//
// So: write a DISTINCT value through every event, then read each NAMED member back. This
// pins event -> member by name rather than by position.
TEST(ToneSlots, EachEventWritesItsOwnNamedPrefMember) {
  NodePrefs p;
  // Values chosen to be distinguishable from each other and from both sentinels
  // (kTuneSilent = 0, kTuneUnset = 0xFF), so a scramble cannot coincide with a default.
  const uint8_t kDm = 21, kCh = 22, kRoom = 23, kNew = 24, kAck = 25, kSent = 26;
  ASSERT_TRUE(setStoredTone(&p, ToneEvent::ContactMessage, kDm));
  ASSERT_TRUE(setStoredTone(&p, ToneEvent::ChannelMessage, kCh));
  ASSERT_TRUE(setStoredTone(&p, ToneEvent::RoomMessage, kRoom));
  ASSERT_TRUE(setStoredTone(&p, ToneEvent::NewContact, kNew));
  ASSERT_TRUE(setStoredTone(&p, ToneEvent::Ack, kAck));
  ASSERT_TRUE(setStoredTone(&p, ToneEvent::Sent, kSent));

  EXPECT_EQ(kDm,   p.ui_tone_dm)         << "ContactMessage no longer writes ui_tone_dm";
  EXPECT_EQ(kCh,   p.ui_tone_channel)    << "ChannelMessage no longer writes ui_tone_channel";
  EXPECT_EQ(kRoom, p.ui_tone_room)       << "RoomMessage no longer writes ui_tone_room";
  EXPECT_EQ(kNew,  p.ui_tone_newcontact) << "NewContact no longer writes ui_tone_newcontact";
  EXPECT_EQ(kAck,  p.ui_tone_ack)        << "Ack no longer writes ui_tone_ack";
  EXPECT_EQ(kSent, p.ui_tone_sent)       << "Sent no longer writes ui_tone_sent";
}

// The read side, pinned the same way: set each NAMED member directly and ask for it by
// event. A reorder breaks writes and reads together, so both directions are asserted
// rather than trusting that one implies the other.
TEST(ToneSlots, EachEventReadsItsOwnNamedPrefMember) {
  NodePrefs p;
  p.ui_tone_dm = 31;
  p.ui_tone_channel = 32;
  p.ui_tone_room = 33;
  p.ui_tone_newcontact = 34;
  p.ui_tone_ack = 35;
  p.ui_tone_sent = 36;

  EXPECT_EQ(31, storedTone(&p, ToneEvent::ContactMessage));
  EXPECT_EQ(32, storedTone(&p, ToneEvent::ChannelMessage));
  EXPECT_EQ(33, storedTone(&p, ToneEvent::RoomMessage));
  EXPECT_EQ(34, storedTone(&p, ToneEvent::NewContact));
  EXPECT_EQ(35, storedTone(&p, ToneEvent::Ack));
  EXPECT_EQ(36, storedTone(&p, ToneEvent::Sent));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
