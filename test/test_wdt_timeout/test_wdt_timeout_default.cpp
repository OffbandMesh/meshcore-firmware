// Native unit tests for WDT_TIMEOUT_SECS (#1083) -- the one timeout every role
// passes to startWatchdog(). Two translation units: this one takes the default,
// test_wdt_timeout_override.cpp defines the macro before the include, so the
// #ifndef guard in MeshCore.h is exercised in both directions.

#include <gtest/gtest.h>
#include "MeshCore.h"

TEST(WdtTimeout, DefaultsToThirtySeconds) {
  EXPECT_EQ(30u, (unsigned)WDT_TIMEOUT_SECS);
}

TEST(WdtTimeout, DefaultIsUsableAsTheStartWatchdogArgument) {
  // startWatchdog takes uint32_t seconds; the macro must be an integral
  // expression that survives that conversion unchanged.
  uint32_t secs = WDT_TIMEOUT_SECS;
  EXPECT_EQ(30u, secs);
}

// A board with no runtime watchdog inherits the no-op pair; the roles call
// these unconditionally now (#1083), so the defaults must be callable.
namespace {
class NoWatchdogBoard : public mesh::MainBoard {
 public:
  uint16_t getBattMilliVolts() override { return 0; }
  const char* getManufacturerName() const override { return "test"; }
  void reboot() override {}
  uint8_t getStartupReason() const override { return BD_STARTUP_NORMAL; }
};
}  // namespace

TEST(WdtTimeout, DefaultBoardAcceptsStartAndFeedAsNoOps) {
  NoWatchdogBoard b;
  b.startWatchdog(WDT_TIMEOUT_SECS);
  b.feedWatchdog();
  SUCCEED();
}

// Each native suite links its own entry point (no gtest_main in this harness).
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
