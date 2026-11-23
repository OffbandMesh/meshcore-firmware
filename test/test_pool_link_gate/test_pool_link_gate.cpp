// Native tests for the broker pool's link gate (#2232,
// src/helpers/wifi_observer/PoolLinkGate.h). Pure logic, no Arduino.

#include <gtest/gtest.h>
#include "helpers/wifi_observer/PoolLinkGate.h"

using namespace offband;

TEST(PoolLinkGate, LinkUpDrives) {
  EXPECT_EQ(LinkAction::Drive, linkAction(true, false));  // rising edge
  EXPECT_EQ(LinkAction::Drive, linkAction(true, true));
}

TEST(PoolLinkGate, NoLinkStartsNothing) {
  // The boot case behind the crash loop: the pool starts before WiFi, so the
  // first passes see a link that has never been up.
  EXPECT_EQ(LinkAction::Hold, linkAction(false, false));
}

TEST(PoolLinkGate, LosingTheLinkPausesEveryClientOnce) {
  EXPECT_EQ(LinkAction::PauseAll, linkAction(false, true));
  // The next pass, still down, only holds; the pause is not repeated.
  EXPECT_EQ(LinkAction::Hold, linkAction(false, false));
}

TEST(PoolLinkGate, BurstCycleSequence) {
  // A repeater's WiFi burst: down at boot, up, down, up again.
  const bool link[] = {false, false, true, true, false, false, true};
  const LinkAction want[] = {LinkAction::Hold, LinkAction::Hold, LinkAction::Drive,
                             LinkAction::Drive, LinkAction::PauseAll, LinkAction::Hold,
                             LinkAction::Drive};
  bool was = false;
  for (size_t i = 0; i < sizeof(link) / sizeof(link[0]); i++) {
    EXPECT_EQ(want[i], linkAction(link[i], was)) << "pass " << i;
    was = link[i];
  }
}

TEST(PoolLinkGate, PoolCausedDisconnectIsNotAFailure) {
  EXPECT_FALSE(disconnectCountsAsFailure(true));
  EXPECT_TRUE(disconnectCountsAsFailure(false));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
