// Offband #2074: what a board may report about its LED.
//
// The weight is on the ASYMMETRY. The enable flag and the ability to honor it are separate
// facts, and the dangerous direction is reporting the flag when the board cannot act on it:
// `MainBoard::setLedEnabled()` returns false and changes nothing, that return is discarded at
// call sites, and a caller holding its own copy of the setting then believes the LED is off.
// If the report echoed that belief, a board would say "LED off" while it was visibly lit --
// which is the class of defect this whole Feature exists to remove (#2065, #1992, #2048).

#include <gtest/gtest.h>

#include <helpers/LedEnable.h>

using namespace offband;

TEST(LedEnable, AControllableLedReportsWhatWasAsked) {
  EXPECT_TRUE(ledEnabledReport(true, true));
  EXPECT_FALSE(ledEnabledReport(true, false));
}

TEST(LedEnable, AnUncontrollableLedReportsENABLEDWhateverWasAsked) {
  // The load-bearing case. "Not controllable" is not "off" -- the LED is behaving normally,
  // so that is what the board must say, even when a caller has recorded otherwise.
  EXPECT_TRUE(ledEnabledReport(false, true));
  EXPECT_TRUE(ledEnabledReport(false, false))
      << "a board that cannot darken its LED must never report it dark";
}

TEST(LedEnable, AnUncontrollableLedStillLightsOnTransmit) {
  // The behavior half, and it must agree with the report half: a board that cannot be asked
  // to stay dark keeps doing exactly what it did before the capability existed.
  EXPECT_TRUE(ledLightsOnTransmit(false, false));
  EXPECT_TRUE(ledLightsOnTransmit(false, true));
}

TEST(LedEnable, AControllableLedObeysOnTransmit) {
  EXPECT_TRUE(ledLightsOnTransmit(true, true));
  EXPECT_FALSE(ledLightsOnTransmit(true, false));
}

TEST(LedEnable, WhatIsReportedIsWhatTheTransmitHookDoes) {
  // Pinned as its own property. These two answers are consumed by different code -- one by a
  // client reading isLedEnabled(), one by onBeforeTransmit() -- and a divergence between them
  // is invisible from either side alone: the board would report one thing and do another.
  for (int c = 0; c < 2; c++) {
    for (int f = 0; f < 2; f++) {
      const bool can = (c != 0);
      const bool flag = (f != 0);
      EXPECT_EQ(ledEnabledReport(can, flag), ledLightsOnTransmit(can, flag))
          << "report and behavior disagree at can_control=" << c << " flag=" << f;
    }
  }
}

TEST(LedEnable, TheDefaultStateIsLitSoNothingGoesDarkOnUpgrade) {
  // A board's flag defaults to true, so a radio that has never been asked anything keeps its
  // LED behaving as it always did. An inverted default would darken every LED in the field on
  // the upgrade that introduced the capability.
  EXPECT_TRUE(ledEnabledReport(true, true));
  EXPECT_TRUE(ledLightsOnTransmit(true, true));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
