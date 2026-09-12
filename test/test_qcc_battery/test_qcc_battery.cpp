#include <gtest/gtest.h>
#include "../../variants/qcc_badge/QccBattery.h"

TEST(QccBattery, DividerRatioMatchesTheSchematic) {
  // R4 680k over R5 1M (QCC 0x4 schematic), the same literal the env hands SafeBoot.
  EXPECT_FLOAT_EQ(1.68f, qcc::kDividerRatio);
}

TEST(QccBattery, MilliVoltsPerCountFoldsTheRatioAndTheReference) {
  // 1.68 * 3600 mV / 4096 counts (internal 0.6 V reference, gain 1/6, 12-bit).
  // 1.68f is not exact in binary, so compare with a tolerance, not FLOAT_EQ.
  EXPECT_NEAR(1.4765625f, qcc::kBattMvPerCount, 1e-6);
}

TEST(QccBattery, AFullCellReadsFourPointTwoVolts) {
  // 4200 mV / 1.68 = 2500 mV at the pin = 2844.4 counts; the ADC reports 2844.
  // One count is 1.48 mV, so 4199 is the nearest reading to 4200.
  EXPECT_EQ(4199, qcc::battMilliVolts(2844));
}

TEST(QccBattery, TheSleepThresholdLandsWhereExpected) {
  // 3500 mV / 1.68 = 2083 mV at the pin = 2370.4 counts; the ADC reports 2370.
  EXPECT_EQ(3499, qcc::battMilliVolts(2370));
}

TEST(QccBattery, RoundsToTheNearestMilliVolt) {
  EXPECT_EQ(3, qcc::battMilliVolts(2));   // 2.95 mV: truncation would give 2
  EXPECT_EQ(4, qcc::battMilliVolts(3));   // 4.43 mV
}

TEST(QccBattery, ZeroCountsIsZero) {
  EXPECT_EQ(0, qcc::battMilliVolts(0));
}

TEST(QccBattery, FullScaleFitsInSixteenBits) {
  // The largest 12-bit average, 4095 counts = 6046.5 mV.
  EXPECT_EQ(6047, qcc::battMilliVolts(4095));
}

TEST(QccBattery, SampleTimeCoversTheDividerImpedance) {
  // 680k || 1M = ~405k source impedance; 40 us is the longest SAADC acquisition.
  EXPECT_EQ(40u, qcc::kAdcSampleUs);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
