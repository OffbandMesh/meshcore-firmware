// Offband #2068: the indicator capability truth table.
//
// The defect these tests exist to prevent was untestable by construction: a
// `#ifdef DISPLAY_CLASS` gate, satisfied on 15 variants by a stub display driver that
// reports no screen. Three boards in three families shipped advertising indicator control
// they could not honor (#2065), and a display SET on a screenless board was persisted and
// echoed as success (#1992's shape).
//
// The weight here is on the FOUR CORNERS, and on the asymmetry between the capability bit
// and the tags: the bit is deliberately an OR and the tags are deliberately independent.
// Getting the bit wrong in either direction is a shipped regression -- AND would hide a
// working control from older clients, and "always true" is the bug being fixed.

#include <gtest/gtest.h>

#include <helpers/IndicatorCaps.h>

using namespace offband;

// --- the two tags are independent ------------------------------------------------------

TEST(IndicatorCaps, DisplayTagFollowsTheDisplayAndNothingElse) {
  EXPECT_TRUE(advertisesDisplayControl(true));
  EXPECT_FALSE(advertisesDisplayControl(false))
      << "a stub display driver must not produce a display-control tag";
}

TEST(IndicatorCaps, LedTagFollowsTheBoardAndNothingElse) {
  EXPECT_TRUE(advertisesLedControl(true));
  EXPECT_FALSE(advertisesLedControl(false))
      << "canControlLed() is false on MeshCore's base class; that must mean no tag";
}

TEST(IndicatorCaps, AScreenDoesNotImplyAControllableLed) {
  // The T096 and the QCC badge: real display, no LED control implemented. This is the
  // exact combination that advertised LED control and then refused the write.
  EXPECT_TRUE(advertisesDisplayControl(true));
  EXPECT_FALSE(advertisesLedControl(false));
}

TEST(IndicatorCaps, AControllableLedDoesNotImplyAScreen) {
  // The inverse, which a headless board with a TX LED will be once #2073 lands.
  EXPECT_FALSE(advertisesDisplayControl(false));
  EXPECT_TRUE(advertisesLedControl(true));
}

// --- the capability bit is an OR, in all four corners -----------------------------------

TEST(IndicatorCaps, TheBitIsSetWhenEitherCapabilityIsPresent) {
  EXPECT_TRUE(indicatorsCapabilityBit(true, true))    << "Heltec V4: display and LED";
  EXPECT_TRUE(indicatorsCapabilityBit(true, false))   << "T096, QCC badge: display only";
  EXPECT_TRUE(indicatorsCapabilityBit(false, true))   << "headless board with a TX LED";
}

TEST(IndicatorCaps, TheBitIsClearOnlyWhenNEITHERIsPresent) {
  EXPECT_FALSE(indicatorsCapabilityBit(false, false))
      << "SenseCAP T1000-E: stub display, no controllable LED. This is the case that "
         "used to set the bit unconditionally and is the whole reason for #2065";
}

TEST(IndicatorCaps, TheBitIsNotAnAnd) {
  // Pinned as its own fact. An AND would look reasonable -- "advertise only if both work"
  // -- and would silently remove display control from every client that reads the bit
  // rather than the tags, on every board with a screen and no LED control. That is most
  // boards with a screen.
  EXPECT_TRUE(indicatorsCapabilityBit(true, false));
  EXPECT_TRUE(indicatorsCapabilityBit(false, true));
}

// --- refusals mirror the capability, so nothing is accepted and ignored -----------------

TEST(IndicatorCaps, ADisplaySubCodeIsRefusedExactlyWhenThereIsNoDisplay) {
  EXPECT_TRUE(refusesDisplaySubCode(false));
  EXPECT_FALSE(refusesDisplaySubCode(true));
}

TEST(IndicatorCaps, ALedSubCodeIsRefusedExactlyWhenTheLedCannotBeDriven) {
  EXPECT_TRUE(refusesLedSubCode(false));
  EXPECT_FALSE(refusesLedSubCode(true));
}

TEST(IndicatorCaps, WhatIsAdvertisedIsExactlyWhatIsNotRefused) {
  // The property that makes the pair honest: a board never advertises a capability whose
  // sub-code it will refuse, and never refuses one it advertised. A silent accept is the
  // #1992 defect; an advertised-then-refused write is #2065. Both are this invariant
  // breaking, in opposite directions.
  for (int d = 0; d < 2; d++) {
    for (int l = 0; l < 2; l++) {
      const bool has_display = (d != 0);
      const bool can_led = (l != 0);
      EXPECT_EQ(advertisesDisplayControl(has_display), !refusesDisplaySubCode(has_display))
          << "display mismatch at has_display=" << d;
      EXPECT_EQ(advertisesLedControl(can_led), !refusesLedSubCode(can_led))
          << "led mismatch at can_control_led=" << l;
    }
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
