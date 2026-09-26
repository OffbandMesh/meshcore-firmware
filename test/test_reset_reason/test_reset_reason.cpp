// #1075: native tests for the shared reset-reason tables. Pure lookup --
// no Arduino, no IDF. The native build takes the non-ESP32 ROM table, which
// is the S3/C3 family the RC32 belongs to.

#include <gtest/gtest.h>
#include <string.h>
#include "helpers/diagnostics/ResetReason.h"

using namespace offband::reset;

TEST(ResetReasonEsp, NamesTheCodesThatUsedToPrintUnknown) {
  // The whole point: 11 is what a bench port-open reports on IDF 5 (#1036).
  EXPECT_STREQ("USB_PERIPHERAL", espToken(11));
  EXPECT_STREQ("JTAG", espToken(12));
  EXPECT_STREQ("EFUSE_ERROR", espToken(13));
  EXPECT_STREQ("PWR_GLITCH", espToken(14));
  EXPECT_STREQ("CPU_LOCKUP", espToken(15));
}

TEST(ResetReasonEsp, KeepsTheCodesThatAlreadyWorked) {
  EXPECT_STREQ("UNKNOWN", espToken(0));
  EXPECT_STREQ("POWERON", espToken(1));
  EXPECT_STREQ("PANIC", espToken(4));
  EXPECT_STREQ("TASK_WDT", espToken(6));     // 6, not 7 -- observed on hardware
  EXPECT_STREQ("BROWNOUT", espToken(9));
  EXPECT_STREQ("SDIO", espToken(10));
}

TEST(ResetReasonEsp, AnUnlistedCodeIsNotCalledUnknown) {
  // "UNKNOWN" is a real reset reason (0). Anything unlisted must not claim it.
  EXPECT_STREQ("UNRECOGNIZED", espToken(99));
  EXPECT_STRNE("UNKNOWN", espToken(99));
  EXPECT_STREQ("Unrecognized reset code", espPhrase(99));
}

TEST(ResetReasonEsp, TokenAndPhraseCoverTheSameCodes) {
  for (uint32_t c = 0; c < 16; c++) {
    EXPECT_NE(nullptr, findEsp(c)) << "code " << c;
    EXPECT_STRNE("UNRECOGNIZED", espToken(c));
    EXPECT_STRNE("Unrecognized reset code", espPhrase(c));
  }
}

TEST(ResetReasonRom, NamesTheUsbHostResetTheRc32Reports) {
  // IDF 4.4 reports ESP_RST_UNKNOWN for this; the ROM code is what names it.
  EXPECT_STREQ("USB_UART_CHIP_RESET", romToken(21));
  EXPECT_STREQ("RTC_SW_CPU_RESET", romToken(12));   // the panic/watchdog path
  EXPECT_STREQ("POWERON_RESET", romToken(1));
  EXPECT_STREQ("RTCWDT_BROWN_OUT_RESET", romToken(15));
}

TEST(ResetReasonRom, SaysNothingRatherThanGuessAtAnUnlistedCode) {
  EXPECT_EQ(nullptr, romToken(2));    // not in the S3/C3 table
  EXPECT_EQ(nullptr, romToken(99));
}

TEST(ResetReasonRom, TablesHoldNoDuplicateCodes) {
  // Both ROM tables are checked, including the one this build does not select,
  // which no target-specific test would ever reach.
  for (size_t i = 0; i < kRomReasonsEsp32Count; i++) {
    for (size_t j = i + 1; j < kRomReasonsEsp32Count; j++) {
      EXPECT_NE(kRomReasonsEsp32[i].code, kRomReasonsEsp32[j].code);
    }
  }
  for (size_t i = 0; i < kRomReasonsNewerCount; i++) {
    for (size_t j = i + 1; j < kRomReasonsNewerCount; j++) {
      EXPECT_NE(kRomReasonsNewer[i].code, kRomReasonsNewer[j].code);
    }
  }
  for (size_t i = 0; i < kEspReasonCount; i++) {
    for (size_t j = i + 1; j < kEspReasonCount; j++) {
      EXPECT_NE(kEspReasons[i].code, kEspReasons[j].code);
    }
  }
}

TEST(ResetReasonRom, TheTwoFamiliesDisagreeWhereTheSiliconDoes) {
  // The reason there are two tables: the same number means different things.
  auto find = [](const Reason* t, size_t n, uint8_t code) -> const Reason* {
    for (size_t i = 0; i < n; i++) if (t[i].code == code) return &t[i];
    return nullptr;
  };
  // 21 is a USB-UART reset on the newer parts and absent on the classic ESP32.
  EXPECT_NE(nullptr, find(kRomReasonsNewer, kRomReasonsNewerCount, 21));
  EXPECT_EQ(nullptr, find(kRomReasonsEsp32, kRomReasonsEsp32Count, 21));
  // 14 is "reset by the other CPU" on the classic ESP32 and absent elsewhere.
  EXPECT_NE(nullptr, find(kRomReasonsEsp32, kRomReasonsEsp32Count, 14));
  EXPECT_EQ(nullptr, find(kRomReasonsNewer, kRomReasonsNewerCount, 14));
  // Both name the same thing for the codes they share.
  EXPECT_STREQ("POWERON_RESET", find(kRomReasonsEsp32, kRomReasonsEsp32Count, 1)->token);
  EXPECT_STREQ("POWERON_RESET", find(kRomReasonsNewer, kRomReasonsNewerCount, 1)->token);
}

TEST(ResetReasonFormat, NamesTheRomCodeWhenTheIdfLayerCannot) {
  char buf[80];
  // The RC32 case: IDF 4.4 says UNKNOWN, the ROM says a host opened the port.
  formatToken(0, 21, true, buf, sizeof(buf));
  EXPECT_STREQ("UNKNOWN(rom:USB_UART_CHIP_RESET)", buf);
  formatPhrase(0, 21, true, buf, sizeof(buf));
  EXPECT_STREQ("Unknown or first boot, ROM says USB_UART_CHIP_RESET", buf);
}

TEST(ResetReasonFormat, KeepsTheNumberOfEveryCodeItCannotName) {
  char buf[80];
  formatToken(99, 0, true, buf, sizeof(buf));
  EXPECT_STREQ("UNRECOGNIZED(99)", buf);
  formatPhrase(99, 0, true, buf, sizeof(buf));
  EXPECT_STREQ("Unrecognized reset code (99)", buf);
  // An unlisted ROM code is printed as a number, not dropped.
  formatToken(0, 200, true, buf, sizeof(buf));
  EXPECT_STREQ("UNKNOWN(rom:200)", buf);
  formatPhrase(0, 200, true, buf, sizeof(buf));
  EXPECT_STREQ("Unknown or first boot, ROM code 200", buf);
}

TEST(ResetReasonFormat, NoRomTableMeansNoRomSuffix) {
  char buf[80];
  formatToken(0, 21, false, buf, sizeof(buf));
  EXPECT_STREQ("UNKNOWN", buf);
  formatPhrase(0, 21, false, buf, sizeof(buf));
  EXPECT_STREQ("Unknown or first boot", buf);
}

TEST(ResetReasonFormat, ANamedCodeNeverCarriesARomSuffix) {
  char buf[80];
  formatToken(6, 21, true, buf, sizeof(buf));    // TASK_WDT knows what it is
  EXPECT_STREQ("TASK_WDT", buf);
  formatPhrase(4, 21, true, buf, sizeof(buf));   // PANIC likewise
  EXPECT_STREQ("Panic / exception reset", buf);
}

TEST(ResetReasonFormat, TinyAndNullBuffersAreSafe) {
  char tiny[4];
  formatToken(6, 0, true, tiny, sizeof(tiny));
  EXPECT_EQ(3u, strlen(tiny));       // truncated, still terminated
  formatToken(6, 0, true, nullptr, 0);   // no crash
}

TEST(ResetReasonUnknown, OnlyCodeZeroTriggersTheRomFallback) {
  EXPECT_TRUE(espReasonIsUnknown(0));
  EXPECT_FALSE(espReasonIsUnknown(1));
  EXPECT_FALSE(espReasonIsUnknown(11));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
