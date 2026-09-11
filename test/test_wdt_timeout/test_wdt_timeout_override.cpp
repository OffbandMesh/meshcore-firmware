// #1083: an env raises the watchdog timeout with -D WDT_TIMEOUT_SECS=n. This TU
// defines it before the include, the way a build flag would, and checks the
// header's #ifndef guard leaves it alone.

#define WDT_TIMEOUT_SECS 90

#include <gtest/gtest.h>
#include "MeshCore.h"

TEST(WdtTimeoutOverride, BuildFlagWins) {
  EXPECT_EQ(90u, (unsigned)WDT_TIMEOUT_SECS);
}
