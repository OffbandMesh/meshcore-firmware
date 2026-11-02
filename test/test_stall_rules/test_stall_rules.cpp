// #1966: when the stall watch reports, and where it reads the stuck task's PC.

#include <gtest/gtest.h>

#include <helpers/diagnostics/StallRules.h>

using namespace offband;

TEST(StallReport, NothingBeforeFifteenSeconds) {
  EXPECT_FALSE(stallReportDue(0, 0));
  EXPECT_FALSE(stallReportDue(14999, 0));
}

TEST(StallReport, ThreeReportsFiveSecondsApart) {
  EXPECT_TRUE(stallReportDue(15000, 0));
  EXPECT_FALSE(stallReportDue(19999, 1));
  EXPECT_TRUE(stallReportDue(20000, 1));
  EXPECT_TRUE(stallReportDue(25000, 2));
}

TEST(StallReport, NeverAFourth) {
  EXPECT_FALSE(stallReportDue(60000, 3));
}

TEST(StallReport, AllThreeBeforeTheWatchdog) {
  const uint32_t last = kStallFirstReportMs + (kStallMaxReports - 1) * kStallReportGapMs;
  EXPECT_LT(last, 30000u) << "the third report must land before the 30 s watchdog";
}

TEST(StallFrame, NoFpuContextSkipsOnlyTheCoreRegisters) {
  EXPECT_EQ(9u, stallHwFrameWords(0xFFFFFFFDu));   // thread mode, PSP, no FPU
}

TEST(StallFrame, AnFpuContextSkipsTheFloatRegistersToo) {
  EXPECT_EQ(25u, stallHwFrameWords(0xFFFFFFEDu));  // thread mode, PSP, FPU used
}

TEST(StallFrame, PcAndLrSitWhereTheHardwareStacksThem) {
  EXPECT_EQ(5u, kStallFrameLr);
  EXPECT_EQ(6u, kStallFramePc);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
