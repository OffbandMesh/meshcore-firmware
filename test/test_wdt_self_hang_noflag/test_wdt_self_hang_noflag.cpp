// Without WDT_TEST_HANG_AFTER_MS the self-hang predicate is a constant false --
// a release image can never become "due".
//
// Its own suite (own binary) on purpose: wdtTestHangDue() is an inline function
// whose body depends on the flag, so a flag-defined TU and a flag-undefined TU
// linked into ONE test binary would be an ODR violation and the linker would
// keep whichever it saw first. One definition per binary.

#include <gtest/gtest.h>
#include "MeshCore.h"

TEST(WdtSelfHangNoFlag, NeverDue) {
  EXPECT_FALSE(mesh::wdtTestHangDue(0u));
  EXPECT_FALSE(mesh::wdtTestHangDue(90000u));
  EXPECT_FALSE(mesh::wdtTestHangDue(0xFFFFFFFFu));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
