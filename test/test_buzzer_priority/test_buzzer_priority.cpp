// Native unit tests for the tone-priority rule (#1417).
//
// The buzzer itself needs Arduino and the RTTTL library, so it cannot be built here.
// What is tested is the decision `genericBuzzer::play()` now makes before touching the
// hardware: whether an incoming request takes the buzzer from what is playing.
//
// The ordering that matters is the third test: a Settings preview must never silence a
// notification that is already sounding.
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
using offband::TonePriority;

namespace {
constexpr bool kIdle = false;
constexpr bool kBusy = true;
constexpr TonePriority kEvent = TonePriority::Event;
constexpr TonePriority kPreview = TonePriority::Preview;
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

// The whole rule as a table, so a change to one branch cannot pass unnoticed.
TEST(TonePriorityTable, EveryCombination) {
  struct Case {
    bool is_playing;
    TonePriority playing;
    TonePriority incoming;
    bool takes;
  };
  const Case cases[] = {
      {kIdle, kEvent, kEvent, true},     {kIdle, kEvent, kPreview, true},
      {kIdle, kPreview, kEvent, true},   {kIdle, kPreview, kPreview, true},
      {kBusy, kEvent, kEvent, true},     {kBusy, kEvent, kPreview, false},
      {kBusy, kPreview, kEvent, true},   {kBusy, kPreview, kPreview, true},
  };
  for (const Case& c : cases) {
    EXPECT_EQ(c.takes, toneTakesBuzzer(c.is_playing, c.playing, c.incoming))
        << "is_playing=" << c.is_playing
        << " playing=" << static_cast<int>(c.playing)
        << " incoming=" << static_cast<int>(c.incoming);
  }
}

// Exactly one combination is a refusal. If a later change makes the rule stricter or
// looser, this count moves and says so.
TEST(TonePriorityTable, OnlyOneCombinationIsRefused) {
  int refused = 0;
  for (bool busy : {false, true}) {
    for (TonePriority playing : {kPreview, kEvent}) {
      for (TonePriority incoming : {kPreview, kEvent}) {
        if (!toneTakesBuzzer(busy, playing, incoming)) refused++;
      }
    }
  }
  EXPECT_EQ(1, refused);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
