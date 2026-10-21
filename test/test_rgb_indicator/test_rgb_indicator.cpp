// Native tests for the RGB indicator's policy (#1558).
//
// There is no RGB LED on any board yet. These exist because the policy is the part that
// does NOT depend on which part gets fitted, and the part a person actually experiences:
// which states are shown, which colour each is, and who wins when two are true at once.
// Deciding and pinning that now is the whole value of the task; the driver waits until
// somebody says whether the badge carries an addressable part or a three-pin PWM one.

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <vector>

#include <helpers/ui/RgbIndicator.h>

using namespace offband;

// ---- priority: the only real decision in here ------------------------------------------

// Unread must beat Traffic. In a busy mesh traffic is near-constant, so letting it mask a
// waiting message would make the indicator useless exactly when it matters most.
TEST(RgbPriority, AWaitingMessageBeatsPassingTraffic) {
  EXPECT_EQ(RgbState::Unread, rgbStateFor(false, true, true));
  EXPECT_EQ(RgbState::Traffic, rgbStateFor(false, false, true));
}

// Low battery beats everything, because the other states stop mattering when the badge
// dies mid-conference.
TEST(RgbPriority, LowBatteryOutranksEverything) {
  EXPECT_EQ(RgbState::LowBattery, rgbStateFor(true, true, true));
  EXPECT_EQ(RgbState::LowBattery, rgbStateFor(true, false, false));
}

TEST(RgbPriority, NothingToSayIsDark) {
  EXPECT_EQ(RgbState::Dark, rgbStateFor(false, false, false));
  EXPECT_EQ(RgbColor({0, 0, 0}), rgbColorFor(RgbState::Dark));
}

// Every combination resolves, and resolves to exactly the highest state that is true.
// Written as a table because the priority order IS the design, and a reordering should
// fail here rather than be discovered on a badge.
TEST(RgbPriority, EveryCombinationResolvesToTheHighestTrueState) {
  struct Case { bool low, unread, traffic; RgbState want; };
  const Case cases[] = {
      {false, false, false, RgbState::Dark},
      {false, false, true,  RgbState::Traffic},
      {false, true,  false, RgbState::Unread},
      {false, true,  true,  RgbState::Unread},
      {true,  false, false, RgbState::LowBattery},
      {true,  false, true,  RgbState::LowBattery},
      {true,  true,  false, RgbState::LowBattery},
      {true,  true,  true,  RgbState::LowBattery},
  };
  for (const auto& c : cases) {
    EXPECT_EQ(c.want, rgbStateFor(c.low, c.unread, c.traffic))
        << "low=" << c.low << " unread=" << c.unread << " traffic=" << c.traffic;
  }
}

// ---- the brightness ceiling ------------------------------------------------------------

// Not a style rule. A badge RGB at full white draws roughly 60 mA against the board's
// measured 13.7 mA idle (#1553) -- an indicator left bright would be the largest consumer
// on the badge, more than the GPS this Feature's other half exists to duty-cycle.
TEST(RgbBrightness, NoColourChannelEverExceedsTheCeiling) {
  for (auto s : {RgbState::Dark, RgbState::Traffic, RgbState::Unread, RgbState::LowBattery}) {
    const RgbColor c = rgbColorFor(s);
    EXPECT_LE(c.r, kRgbMaxLevel) << rgbStateName(s);
    EXPECT_LE(c.g, kRgbMaxLevel) << rgbStateName(s);
    EXPECT_LE(c.b, kRgbMaxLevel) << rgbStateName(s);
  }
}

TEST(RgbBrightness, ScalingIsMonotonicAndCannotOverflow) {
  uint8_t prev = 0;
  for (int full = 0; full <= 255; full++) {
    const uint8_t v = rgbLevel((uint8_t)full);
    EXPECT_LE(v, kRgbMaxLevel) << "full=" << full;
    EXPECT_GE(v, prev) << "scaling went backwards at full=" << full;
    prev = v;
  }
  EXPECT_EQ(0, rgbLevel(0));
  EXPECT_EQ(kRgbMaxLevel, rgbLevel(255));
}

// ---- the colours are distinguishable ---------------------------------------------------

// An indicator whose states look alike is worse than none: it invites a wrong reading.
// Across a room, at a quarter brightness, these have to be four different things.
TEST(RgbColours, EveryStateLooksDifferent) {
  std::vector<RgbColor> seen;
  for (auto s : {RgbState::Dark, RgbState::Traffic, RgbState::Unread, RgbState::LowBattery}) {
    const RgbColor c = rgbColorFor(s);
    for (const auto& other : seen) {
      EXPECT_FALSE(c == other) << rgbStateName(s) << " renders the same as another state";
    }
    seen.push_back(c);
  }
}

// Red is spent on low battery and nothing else, so red always means one thing. A state
// that crept into red-dominant territory would quietly overload it.
TEST(RgbColours, RedIsReservedForLowBattery) {
  const RgbColor low = rgbColorFor(RgbState::LowBattery);
  EXPECT_GT(low.r, 0);
  EXPECT_EQ(0, low.g);
  EXPECT_EQ(0, low.b);

  // Unread is amber -- it has red in it, but green with it, so it is not read as an alarm.
  const RgbColor unread = rgbColorFor(RgbState::Unread);
  EXPECT_GT(unread.g, 0) << "unread lost its green and now reads as a low-battery alarm";

  // Traffic carries no red at all: ambient, and clearly not a warning.
  EXPECT_EQ(0, rgbColorFor(RgbState::Traffic).r);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
