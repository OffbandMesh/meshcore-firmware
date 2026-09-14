#include <gtest/gtest.h>
#include <string.h>
#include "helpers/PadBeacon.h"

using offband::formatPadBeaconLine;

TEST(PadBeacon, NamesThePadAndCountsTheRound) {
  char buf[32];
  const size_t len = formatPadBeaconLine(buf, sizeof buf, "GPIO34 P1.02", 17);
  EXPECT_STREQ("PAD GPIO34 P1.02 #17\r\n", buf);
  EXPECT_EQ(strlen(buf), len);
}

TEST(PadBeacon, TheLongestLineFitsTheBeaconBuffer) {
  // padBeaconTick() formats into a 32-byte buffer. The widest pad name and the
  // largest round number must still fit, or the beacon would skip that pad.
  char buf[32];
  EXPECT_GT(formatPadBeaconLine(buf, sizeof buf, "GPIO39 P1.07", 4294967295UL), 0u);
  EXPECT_STREQ("PAD GPIO39 P1.07 #4294967295\r\n", buf);
}

TEST(PadBeacon, ALineThatDoesNotFitIsNotSentAtAll) {
  // A truncated ID could name the wrong pad, so it is dropped, never cut short.
  char buf[12];
  memset(buf, 'x', sizeof buf);
  EXPECT_EQ(0u, formatPadBeaconLine(buf, sizeof buf, "GPIO34 P1.02", 17));
  EXPECT_STREQ("", buf);
}

TEST(PadBeacon, NullInputsProduceNothing) {
  char buf[32] = "stale";
  EXPECT_EQ(0u, formatPadBeaconLine(buf, sizeof buf, nullptr, 1));
  EXPECT_STREQ("", buf);
  EXPECT_EQ(0u, formatPadBeaconLine(nullptr, sizeof buf, "GPIO33 P1.01", 1));
  EXPECT_EQ(0u, formatPadBeaconLine(buf, 0, "GPIO33 P1.01", 1));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
