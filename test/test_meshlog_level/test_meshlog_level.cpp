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

// #1069: the UART0 wire ceiling. Default build = MLOG_DEBUG, so every level up
// to DEBUG reaches the wire and the MLOG_PACKET firehose does not.
TEST(MeshLogUart0Ceiling, DefaultAdmitsThroughDebug) {
  EXPECT_EQ(MLOG_DEBUG, (uint8_t)(OFFBAND_LOG_MIRROR_LEVEL));
  EXPECT_TRUE(meshLogUart0Admits(MLOG_BOOT));
  EXPECT_TRUE(meshLogUart0Admits(MLOG_ERROR));
  EXPECT_TRUE(meshLogUart0Admits(MLOG_DEBUG));
}

TEST(MeshLogUart0Ceiling, DefaultRefusesPacketAndAbove) {
  EXPECT_FALSE(meshLogUart0Admits(MLOG_PACKET));
  EXPECT_FALSE(meshLogUart0Admits(200));
}

// #1069: the full routing decision mesh_log_line() makes, every combination.
TEST(MeshLogRoute, RuntimeLevelGatesBothSinks) {
  // Above the runtime level: nothing, whatever else is on.
  MeshLogRoute r = meshLogRoute(MLOG_DEBUG, MLOG_ERROR, true, true);
  EXPECT_FALSE(r.capture);
  EXPECT_FALSE(r.wire);
}

TEST(MeshLogRoute, CaptureOffStillReachesTheWire) {
  // #763: the wire must not depend on someone enabling capture first.
  MeshLogRoute r = meshLogRoute(MLOG_BOOT, MLOG_DEBUG, false, true);
  EXPECT_FALSE(r.capture);
  EXPECT_TRUE(r.wire);
}

TEST(MeshLogRoute, NoUart0BuildNeverRoutesToTheWire) {
  MeshLogRoute r = meshLogRoute(MLOG_BOOT, MLOG_DEBUG, true, false);
  EXPECT_TRUE(r.capture);
  EXPECT_FALSE(r.wire);
  r = meshLogRoute(MLOG_BOOT, MLOG_DEBUG, false, false);
  EXPECT_FALSE(r.capture);
  EXPECT_FALSE(r.wire);
}

TEST(MeshLogRoute, PacketLevelIsCapturedButHeldOffTheWire) {
  // The case #1069 exists for: a client raises capture to `packet`.
  MeshLogRoute r = meshLogRoute(MLOG_PACKET, MLOG_PACKET, true, true);
  EXPECT_TRUE(r.capture);
  EXPECT_FALSE(r.wire);
}

TEST(MeshLogRoute, DebugAtDefaultCeilingReachesBoth) {
  MeshLogRoute r = meshLogRoute(MLOG_DEBUG, MLOG_PACKET, true, true);
  EXPECT_TRUE(r.capture);
  EXPECT_TRUE(r.wire);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
