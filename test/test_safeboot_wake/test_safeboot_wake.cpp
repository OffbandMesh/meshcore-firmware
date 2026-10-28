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

#include <helpers/SafeBootDisplay.h>
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

// ---- #1889: the always-powered display goes to sleep first ----------------------------

TEST(DisplaySleep, TheStreamIsCommandsThenDisplayOffThenChargePumpOff) {
  // SSD1306 datasheet: control byte 0x00 = a command stream; 0xAE = display OFF (sleep);
  // 0x8D 0x10 = charge pump disabled. Display-off comes first so the glass goes dark even
  // if the bus drops the rest.
  const uint8_t want[] = {0x00, 0xAE, 0x8D, 0x10};
  ASSERT_EQ(sizeof(want), sizeof(kSsd1306SleepStream));
  for (size_t i = 0; i < sizeof(want); i++) EXPECT_EQ(want[i], kSsd1306SleepStream[i]) << i;
}

TEST(DisplaySleep, TheStreamNeverTurnsTheDisplayOn) {
  for (uint8_t b : kSsd1306SleepStream) {
    EXPECT_NE(0xAF, b) << "0xAF is display ON";
    EXPECT_NE(0x14, b) << "0x14 after 0x8D is charge pump ON";
  }
}

TEST(DisplaySleep, OnlyRealSevenBitAddressesAreAccepted) {
  EXPECT_TRUE(validI2cAddress(0x3C));
  EXPECT_TRUE(validI2cAddress(0x3D));
  EXPECT_FALSE(validI2cAddress(0x00));   // general call
  EXPECT_FALSE(validI2cAddress(0x07));   // reserved
  EXPECT_FALSE(validI2cAddress(0x78));   // 10-bit prefix
  EXPECT_FALSE(validI2cAddress(0x78 << 1));   // an 8-bit (shifted) address by mistake
}

TEST(DisplaySleep, TheBusIsUsedOnlyWhenBothLinesIdleHigh) {
  EXPECT_TRUE(i2cBusIdle(1, 1));
  EXPECT_FALSE(i2cBusIdle(0, 1));
  EXPECT_FALSE(i2cBusIdle(1, 0));
  EXPECT_FALSE(i2cBusIdle(0, 0));
}

TEST(DisplaySleep, OnlyAHeldDataLineCanBeClockedFree) {
  // A device stopped mid-byte by the reset holds SDA; clocking SCL lets it finish.
  EXPECT_TRUE(i2cBusRecoverable(0, 1));
  // SCL held low cannot be driven from here: leave the display alone, never hang.
  EXPECT_FALSE(i2cBusRecoverable(1, 0));
  EXPECT_FALSE(i2cBusRecoverable(0, 0));
  EXPECT_FALSE(i2cBusRecoverable(1, 1));   // nothing to recover
  EXPECT_EQ(9, kI2cRecoveryClocks);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
