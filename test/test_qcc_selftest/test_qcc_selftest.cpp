#include <gtest/gtest.h>
#include <string.h>
#include "../../variants/qcc_badge/QccSelfTest.h"

TEST(QccSelfTest, ShowsSafeBootsReadingInMilliVolts) {
  char buf[32];
  qcc::formatSafeBootLine(buf, sizeof buf, 4172);
  EXPECT_STREQ("SafeBoot: 4172 mV", buf);
}

TEST(QccSelfTest, ZeroMeansSafeBootHadNoReading) {
  // SafeBoot lets the boot through when its read comes back 0, exactly as when it
  // passes, so 0 must never be shown as a voltage.
  char buf[32];
  qcc::formatSafeBootLine(buf, sizeof buf, 0);
  EXPECT_STREQ("SafeBoot: no reading", buf);
}

TEST(QccSelfTest, EveryLineFitsTheScreen) {
  const uint16_t readings[] = {0, 1, 999, 4172, 65535};
  char buf[64];
  for (uint16_t mv : readings) {
    qcc::formatSafeBootLine(buf, sizeof buf, mv);
    EXPECT_LE(strlen(buf), qcc::kSelfTestLineChars) << buf;
  }
}

TEST(QccSelfTest, AShortBufferTruncatesAndStaysTerminated) {
  char buf[8];
  memset(buf, 'x', sizeof buf);
  qcc::formatSafeBootLine(buf, sizeof buf, 4172);
  EXPECT_STREQ("SafeBoo", buf);
}

TEST(QccSelfTest, SaysWhetherTheKeyboardAnsweredAndHowToTestIt) {
  char buf[32];
  qcc::formatKeyboardLine(buf, sizeof buf, true);
  EXPECT_STREQ("KB: found  TAB=keys", buf);
  qcc::formatKeyboardLine(buf, sizeof buf, false);
  EXPECT_STREQ("KB: none", buf);
}

TEST(QccSelfTest, TheKeyboardLineFitsTheScreen) {
  char buf[64];
  for (bool found : {true, false}) {
    qcc::formatKeyboardLine(buf, sizeof buf, found);
    EXPECT_LE(strlen(buf), qcc::kSelfTestLineChars) << buf;
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
