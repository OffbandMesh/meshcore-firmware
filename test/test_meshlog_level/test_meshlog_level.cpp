// Native unit tests for the MeshLog verbosity level-name helpers (#395) used
// by the `caplog start [level]` CLI verb. Pure inline logic — no Arduino.

#include <gtest/gtest.h>
#include "MeshLog.h"

TEST(MeshLogLevel, NameForEachLevel) {
  EXPECT_STREQ("boot", meshLogLevelName(MLOG_BOOT));
  EXPECT_STREQ("error", meshLogLevelName(MLOG_ERROR));
  EXPECT_STREQ("debug", meshLogLevelName(MLOG_DEBUG));
  EXPECT_STREQ("packet", meshLogLevelName(MLOG_PACKET));
}

TEST(MeshLogLevel, FromNameValid) {
  uint8_t lvl = 99;
  EXPECT_TRUE(meshLogLevelFromName("boot", &lvl));
  EXPECT_EQ(MLOG_BOOT, lvl);
  EXPECT_TRUE(meshLogLevelFromName("packet", &lvl));
  EXPECT_EQ(MLOG_PACKET, lvl);
}

TEST(MeshLogLevel, FromNameInvalidLeavesOutputUnchanged) {
  uint8_t lvl = 42;
  EXPECT_FALSE(meshLogLevelFromName("bogus", &lvl));
  EXPECT_EQ(42, lvl);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
