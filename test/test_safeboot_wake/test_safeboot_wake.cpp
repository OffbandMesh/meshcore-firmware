// #1687: how SafeBoot leaves a low-battery sleep.
//
// The defect these pin was measured, not inferred: on the QCC badge SafeBoot armed its
// button wake on P0.06 -- the buzzer gate, held low -- with a pull-up and SENSE_LOW, so the
// wake condition was true the instant the chip slept and it looped every 128 ms (#1689).
// On the T1000-E the button is active-high, and the same active-low arming did the same.
// The rules here decide the pull, the sense level, whether a button may be armed at all,
// and what the log line may claim.

#include <gtest/gtest.h>

#include <cstring>

#include <helpers/SafeBootWake.h>

using namespace offband;

// ---- the button's polarity sets the pull and the sense ------------------------------

TEST(ButtonSense, AnActiveLowButtonIsPulledUpAndWakesOnLow) {
  const ButtonSense b = chooseButtonSense(/*pressed_high=*/false, /*idle_level=*/1);
  EXPECT_TRUE(b.usable);
  EXPECT_FALSE(b.sense_high);
  EXPECT_FALSE(b.pull_down);
}

TEST(ButtonSense, AnActiveHighButtonIsPulledDownAndWakesOnHigh) {
  // The T1000-E: USER_BTN_PRESSED=HIGH. Arming it pull-up + SENSE_LOW was the bug.
  const ButtonSense b = chooseButtonSense(/*pressed_high=*/true, /*idle_level=*/0);
  EXPECT_TRUE(b.usable);
  EXPECT_TRUE(b.sense_high);
  EXPECT_TRUE(b.pull_down);
}

// ---- a button that already reads pressed must never be armed ------------------------

TEST(ButtonSense, ALineThatReadsPressedAtIdleIsNotUsable) {
  // The QCC measurement: the line SafeBoot armed read 0 under the pull-up. With an
  // active-low wake that is "pressed" before the chip even sleeps -- an immediate wake.
  EXPECT_FALSE(chooseButtonSense(/*pressed_high=*/false, /*idle_level=*/0).usable);
  // And the mirror image for an active-high button that reads high at idle.
  EXPECT_FALSE(chooseButtonSense(/*pressed_high=*/true, /*idle_level=*/1).usable);
}

TEST(ButtonSense, AnUnreadableLevelIsNotUsable) {
  EXPECT_FALSE(chooseButtonSense(false, -1).usable);
  EXPECT_FALSE(chooseButtonSense(true, 2).usable);
}

// ---- which sleep -------------------------------------------------------------------

TEST(Nrf52Wake, TheBatteryComparatorComesFirst) {
  EXPECT_EQ(SafeBootWake::BatteryRise, chooseNrf52Wake(true, true));
  EXPECT_EQ(SafeBootWake::BatteryRise, chooseNrf52Wake(true, false));
}

TEST(Nrf52Wake, AUsableButtonAloneMeansButtonOnly) {
  EXPECT_EQ(SafeBootWake::ButtonOnly, chooseNrf52Wake(false, true));
}

TEST(Nrf52Wake, NoUsableWakeSourceMeansATimedRestartNotALoop) {
  // Before #1687 a board with a button but no comparator always entered SYSTEM_OFF on
  // the button, usable or not. An unusable one must take the timed path instead.
  EXPECT_EQ(SafeBootWake::TimedRestart, chooseNrf52Wake(false, false));
}

// ---- what the log may claim (#1690) --------------------------------------------------

TEST(WakeText, OnlyTheTimedKindsPrintASleepTime) {
  EXPECT_TRUE(safeBootWakeIsTimed(SafeBootWake::TimedDeepSleep));
  EXPECT_TRUE(safeBootWakeIsTimed(SafeBootWake::TimedRestart));
  EXPECT_FALSE(safeBootWakeIsTimed(SafeBootWake::BatteryRise));
  EXPECT_FALSE(safeBootWakeIsTimed(SafeBootWake::ButtonOnly));
}

TEST(WakeText, ASystemOffSleepNeverClaimsATimedWake) {
  for (SafeBootWake w : {SafeBootWake::BatteryRise, SafeBootWake::ButtonOnly}) {
    for (bool armed : {false, true}) {
      const char* t = safeBootWakeText(w, armed);
      EXPECT_EQ(nullptr, std::strstr(t, "after")) << t;
      EXPECT_EQ(nullptr, std::strstr(t, "sleep ")) << t;
    }
  }
}

TEST(WakeText, ABatterySleepMentionsTheButtonOnlyWhenItIsArmed) {
  EXPECT_NE(nullptr, std::strstr(safeBootWakeText(SafeBootWake::BatteryRise, true), "button"));
  EXPECT_EQ(nullptr, std::strstr(safeBootWakeText(SafeBootWake::BatteryRise, false), "button"));
}

TEST(WakeText, ButtonOnlyTellsThePersonWhatToDo) {
  EXPECT_NE(nullptr, std::strstr(safeBootWakeText(SafeBootWake::ButtonOnly, true), "press the button"));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
