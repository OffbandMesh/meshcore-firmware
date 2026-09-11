// Native unit tests for the bench-only self-hang predicate (WDT_TEST_HANG_AFTER_MS).
// This TU defines the flag before the include, the way -D does. The act itself
// (wdtTestHangTick) compiles out on the host (no ARDUINO), so only the predicate
// is exercised here; the source-level test pins the tick's shape.

#define WDT_TEST_HANG_AFTER_MS 90000

#include <gtest/gtest.h>
#include "MeshCore.h"

TEST(WdtSelfHang, NotDueBeforeTheDeadline) {
  EXPECT_FALSE(mesh::wdtTestHangDue(0u));
  EXPECT_FALSE(mesh::wdtTestHangDue(89999u));
}

TEST(WdtSelfHang, DueAtAndAfterTheDeadline) {
  EXPECT_TRUE(mesh::wdtTestHangDue(90000u));
  EXPECT_TRUE(mesh::wdtTestHangDue(0xFFFFFFFFu));
}

// Each native suite links its own entry point (no gtest_main in this harness).
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
