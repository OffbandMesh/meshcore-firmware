// Native unit tests for genericBuzzer::quiet() (#1922), against the real buzzer.cpp and
// a mock RTTTL library.
//
// Asking for quiet has to silence a tune already in progress. On a board with no
// PIN_BUZZER_EN, the QCC badge among them, nothing else stops it: the piezo keeps sounding
// until the tune ends or until the next request cuts it off.

#include <gtest/gtest.h>

#define PIN_BUZZER 7
#include "helpers/ui/buzzer.cpp"

namespace {

constexpr const char* kTune = "t:d=4,o=5,b=100:c";
constexpr const char* kOtherTune = "o:d=4,o=5,b=100:e";

class BuzzerQuiet : public ::testing::Test {
 protected:
  void SetUp() override {
    rtttl::g_mock_playing = false;
    rtttl::g_mock_begin_calls = 0;
    rtttl::g_mock_stop_calls = 0;
    rtttl::g_mock_melody = nullptr;
    buzzer.begin();  // begin() leaves the buzzer un-quiet
  }
  genericBuzzer buzzer;
};

TEST_F(BuzzerQuiet, QuietStopsATuneInProgress) {
  buzzer.play(kTune);
  ASSERT_TRUE(buzzer.isPlaying());

  buzzer.quiet(true);

  EXPECT_FALSE(buzzer.isPlaying());
  EXPECT_EQ(rtttl::g_mock_stop_calls, 1);
  EXPECT_TRUE(buzzer.isQuiet());
}

TEST_F(BuzzerQuiet, QuietStopsEvenTheHighestPriorityTune) {
  buzzer.play(kTune, offband::TonePriority::Trigger);

  buzzer.quiet(true);

  EXPECT_FALSE(buzzer.isPlaying());
}

// The sequence the bug report describes: the request that arrives after quiet finds
// nothing to cut off, and does not start.
TEST_F(BuzzerQuiet, RequestAfterQuietIsRefusedAndStartsNothing) {
  buzzer.play(kTune);
  buzzer.quiet(true);
  const int stops = rtttl::g_mock_stop_calls;

  EXPECT_EQ(buzzer.play(kOtherTune), offband::TonePlay::RefusedQuiet);

  EXPECT_FALSE(buzzer.isPlaying());
  EXPECT_EQ(rtttl::g_mock_begin_calls, 1);
  EXPECT_EQ(rtttl::g_mock_stop_calls, stops);
  EXPECT_STREQ(rtttl::g_mock_melody, kTune);
}

TEST_F(BuzzerQuiet, QuietWithNothingPlayingCallsNoStop) {
  buzzer.quiet(true);

  EXPECT_EQ(rtttl::g_mock_stop_calls, 0);
  EXPECT_TRUE(buzzer.isQuiet());
}

// ui-orig and toggleBuzzer() un-quiet and then play a cue, and ui-orig's cue plays on after
// that call, so leaving quiet must not stop anything.
TEST_F(BuzzerQuiet, UnquietLeavesATuneInProgressAlone) {
  buzzer.play(kTune);

  buzzer.quiet(false);

  EXPECT_TRUE(buzzer.isPlaying());
  EXPECT_EQ(rtttl::g_mock_stop_calls, 0);
}

TEST_F(BuzzerQuiet, PlayWorksAgainAfterLeavingQuiet) {
  buzzer.play(kTune);
  buzzer.quiet(true);
  buzzer.quiet(false);

  EXPECT_EQ(buzzer.play(kOtherTune), offband::TonePlay::Played);

  EXPECT_TRUE(buzzer.isPlaying());
  EXPECT_STREQ(rtttl::g_mock_melody, kOtherTune);
}

// The priority rule still decides while the buzzer is not quiet: a preview does not cut
// off a notification.
TEST_F(BuzzerQuiet, UnquietPreviewStillDoesNotTakeAnEvent) {
  buzzer.play(kTune);

  EXPECT_EQ(buzzer.play(kOtherTune, offband::TonePriority::Preview), offband::TonePlay::RefusedBusy);

  EXPECT_TRUE(buzzer.isPlaying());
  EXPECT_STREQ(rtttl::g_mock_melody, kTune);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
