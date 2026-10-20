// Native unit tests for the tone-priority rule (#1417).
//
// The buzzer itself needs Arduino and the RTTTL library, so it cannot be built here.
// What is tested is the decision `genericBuzzer::play()` now makes before touching the
// hardware: whether an incoming request takes the buzzer from what is playing.
//
// Two orderings carry the weight. A Settings preview must never silence a notification
// that is already sounding (#1417, the reason the rule exists at all). And a
// notification must never cut off a deliberate event tune: the payload runs 17.1 s
// against a quarter-second chime, so without that a busy channel truncates it on every
// badge in the room. Owner ruling: "trigger tune > incoming message".
//
// WHAT THIS DOES NOT COVER, deliberately. `genericBuzzer::play()` wires this rule to
// `rtttl::stop()` / `rtttl::begin()` and that wiring is not exercised here, because the
// class needs Arduino and the RTTTL library. Making it host-testable would mean
// templating or injecting into a file about sixty variants compile, which is a bigger
// change than the rule itself. The wiring is verified on hardware instead: #1423 tests
// preview-versus-notification in both directions on the badge.

#include <gtest/gtest.h>
#include "helpers/ui/BuzzerPriority.h"

using offband::toneTakesBuzzer;
using offband::TonePlay;
using offband::TonePriority;

namespace {
constexpr bool kIdle = false;
constexpr bool kBusy = true;
constexpr TonePriority kEvent = TonePriority::Event;
constexpr TonePriority kPreview = TonePriority::Preview;
constexpr TonePriority kTrigger = TonePriority::Trigger;

// Every level, so the table tests cannot silently keep testing only the old two.
constexpr TonePriority kAll[] = {TonePriority::Preview, TonePriority::Event,
                                 TonePriority::Trigger};
}  // namespace

// An idle buzzer takes anything, and what `playing` happens to hold is not consulted.
TEST(TonePriorityIdle, AnythingPlaysWhenNothingIsPlaying) {
  EXPECT_TRUE(toneTakesBuzzer(kIdle, kEvent, kEvent));
  EXPECT_TRUE(toneTakesBuzzer(kIdle, kEvent, kPreview));
  EXPECT_TRUE(toneTakesBuzzer(kIdle, kPreview, kEvent));
  EXPECT_TRUE(toneTakesBuzzer(kIdle, kPreview, kPreview));
}

// Today's behavior, kept: a notification interrupts a notification. Two messages
// arriving together should sound twice, not swallow the second.
TEST(TonePriorityEvent, EventPreemptsEvent) {
  EXPECT_TRUE(toneTakesBuzzer(kBusy, kEvent, kEvent));
}

// The reason this rule exists. Auditioning a tune while a real chime is sounding must
// not cut the chime off -- that is the one notification the owner cared about.
TEST(TonePriorityEvent, PreviewIsRefusedWhileAnEventPlays) {
  EXPECT_FALSE(toneTakesBuzzer(kBusy, kEvent, kPreview));
}

// A real event still wins over an audition, which is what makes refusing the other
// direction safe: nothing can be made to miss a notification.
TEST(TonePriorityPreview, EventPreemptsPreview) {
  EXPECT_TRUE(toneTakesBuzzer(kBusy, kPreview, kEvent));
}

// Moving down the tune list re-auditions immediately rather than queueing behind the
// previous preview.
TEST(TonePriorityPreview, PreviewReplacesPreview) {
  EXPECT_TRUE(toneTakesBuzzer(kBusy, kPreview, kPreview));
}

// ---- the third level, and the reason it exists ----------------------------------------

// THE POINT OF Trigger. The payload runs 17.1 s; a notification is under a quarter of a
// second. Without this, the next message to arrive in a busy channel truncates the event
// tune on every badge in the room -- and in a busy channel that is every time. This is
// the one assertion the owner ruled on: "trigger tune > incoming message".
TEST(TonePriorityTrigger, ANotificationDoesNotCutOffATrigger) {
  EXPECT_FALSE(toneTakesBuzzer(kBusy, kTrigger, kEvent));
}

// The other direction stays open. A trigger arriving mid-chime wins, because the chime
// repeats on the next message and the event does not happen again.
TEST(TonePriorityTrigger, ATriggerPreemptsANotification) {
  EXPECT_TRUE(toneTakesBuzzer(kBusy, kEvent, kTrigger));
}

// Equal rank replaces equal rank, as it already did for Event: a second trigger restarts
// rather than being dropped on the floor.
TEST(TonePriorityTrigger, ATriggerReplacesATrigger) {
  EXPECT_TRUE(toneTakesBuzzer(kBusy, kTrigger, kTrigger));
}

// An audition is refused against a trigger for the same reason it is refused against a
// notification, and this is the case a "Preview loses only to Event" rule would have let
// through.
TEST(TonePriorityTrigger, APreviewIsRefusedWhileATriggerPlays) {
  EXPECT_FALSE(toneTakesBuzzer(kBusy, kTrigger, kPreview));
}

// A trigger takes an idle buzzer like anything else -- no special case on the way in.
TEST(TonePriorityTrigger, ATriggerTakesAnIdleBuzzer) {
  EXPECT_TRUE(toneTakesBuzzer(kIdle, kTrigger, kTrigger));
  EXPECT_TRUE(toneTakesBuzzer(kIdle, kEvent, kTrigger));
  EXPECT_TRUE(toneTakesBuzzer(kIdle, kTrigger, kEvent));
}

// The whole rule as a table, so a change to one branch cannot pass unnoticed.
TEST(TonePriorityTable, EveryCombination) {
  struct Case {
    bool is_playing;
    TonePriority playing;
    TonePriority incoming;
    bool takes;
  };
  const Case cases[] = {
      // idle: everything plays, and `playing` is not consulted.
      {kIdle, kPreview, kPreview, true},  {kIdle, kPreview, kEvent, true},
      {kIdle, kPreview, kTrigger, true},  {kIdle, kEvent, kPreview, true},
      {kIdle, kEvent, kEvent, true},      {kIdle, kEvent, kTrigger, true},
      {kIdle, kTrigger, kPreview, true},  {kIdle, kTrigger, kEvent, true},
      {kIdle, kTrigger, kTrigger, true},
      // busy: at or above the playing rank takes it, below it does not.
      {kBusy, kPreview, kPreview, true},  {kBusy, kPreview, kEvent, true},
      {kBusy, kPreview, kTrigger, true},  {kBusy, kEvent, kPreview, false},
      {kBusy, kEvent, kEvent, true},      {kBusy, kEvent, kTrigger, true},
      {kBusy, kTrigger, kPreview, false}, {kBusy, kTrigger, kEvent, false},
      {kBusy, kTrigger, kTrigger, true},
  };
  for (const Case& c : cases) {
    EXPECT_EQ(c.takes, toneTakesBuzzer(c.is_playing, c.playing, c.incoming))
        << "is_playing=" << c.is_playing
        << " playing=" << static_cast<int>(c.playing)
        << " incoming=" << static_cast<int>(c.incoming);
  }
}

// Exactly three combinations are refusals, and naming them is the point: a Preview
// against an Event, and both lower levels against a Trigger. If a later change makes the
// rule stricter or looser, this count moves and says so. It moved from 1 to 3 when
// Trigger was added, which is this test doing its job rather than a surprise.
TEST(TonePriorityTable, ExactlyThreeCombinationsAreRefused) {
  int refused = 0;
  for (bool busy : {false, true}) {
    for (TonePriority playing : kAll) {
      for (TonePriority incoming : kAll) {
        if (!toneTakesBuzzer(busy, playing, incoming)) refused++;
      }
    }
  }
  EXPECT_EQ(3, refused);
}

// Every refusal is a strictly-lower request against a busy buzzer, and nothing else ever
// is. Stated as a property rather than a list so a fourth level cannot slip in a refusal
// that does not follow the ordering.
TEST(TonePriorityTable, ARefusalIsAlwaysAndOnlyALowerRankAgainstABusyBuzzer) {
  for (bool busy : {false, true}) {
    for (TonePriority playing : kAll) {
      for (TonePriority incoming : kAll) {
        const bool expected_refusal =
            busy && (uint8_t)incoming < (uint8_t)playing;
        EXPECT_EQ(expected_refusal, !toneTakesBuzzer(busy, playing, incoming))
            << "busy=" << busy << " playing=" << static_cast<int>(playing)
            << " incoming=" << static_cast<int>(incoming);
      }
    }
  }
}

// The enum's order is what the rule compares, so a reordering must not compile. The
// header carries static_asserts for this; restate it here so the intent survives if
// somebody "simplifies" those away.
TEST(TonePriorityOrder, TheLevelsAscendInImportance) {
  EXPECT_LT((uint8_t)TonePriority::Preview, (uint8_t)TonePriority::Event);
  EXPECT_LT((uint8_t)TonePriority::Event, (uint8_t)TonePriority::Trigger);
}

// #1422: the four outcomes are distinct, and NotFound is not Played. A remote trigger
// needs to tell "the badge made no noise" from "the badge played silence", and folding
// them together is what the fourth value exists to prevent.
TEST(TonePlayResult, TheFourOutcomesAreDistinct) {
  const TonePlay all[] = {TonePlay::Played, TonePlay::RefusedBusy,
                          TonePlay::RefusedQuiet, TonePlay::NotFound};
  for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
    for (size_t j = i + 1; j < sizeof(all) / sizeof(all[0]); j++) {
      EXPECT_NE(all[i], all[j]) << "outcomes " << i << " and " << j << " collide";
    }
  }
  EXPECT_NE(TonePlay::NotFound, TonePlay::Played)
      << "a trigger could not tell a silent badge from an unresolvable tune";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
